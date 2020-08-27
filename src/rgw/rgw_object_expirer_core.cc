// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab ft=cpp

#include <errno.h>
#include <iostream>
#include <sstream>
#include <string>


#include "auth/Crypto.h"

#include "common/armor.h"
#include "common/ceph_json.h"
#include "common/config.h"
#include "common/ceph_argparse.h"
#include "common/Formatter.h"
#include "common/errno.h"

#include "global/global_init.h"

#include "include/utime.h"
#include "include/str_list.h"

#include "rgw_user.h"
#include "rgw_bucket.h"
#include "rgw_acl.h"
#include "rgw_acl_s3.h"
#include "rgw_log.h"
#include "rgw_formats.h"
#include "rgw_usage.h"
#include "rgw_object_expirer_core.h"
#include "rgw_zone.h"

#include "services/svc_rados.h"
#include "services/svc_zone.h"
#include "services/svc_sys_obj.h"
#include "services/svc_bi_rados.h"

#include "cls/lock/cls_lock_client.h"
#include "cls/timeindex/cls_timeindex_client.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_rgw

static string objexp_lock_name = "gc_process";

static string objexp_hint_get_shardname(int shard_num)
{
  char buf[64];
  snprintf(buf, sizeof(buf), "obj_delete_at_hint.%010u", (unsigned)shard_num);
  return buf;
}

static int objexp_key_shard(const rgw_obj_index_key& key, int num_shards)
{
  string obj_key = key.name + key.instance;
  return RGWSI_BucketIndex_RADOS::bucket_shard_index(obj_key, num_shards);
}

static string objexp_hint_get_keyext(const string& tenant_name,
                                     const string& bucket_name,
                                     const string& bucket_id,
                                     const rgw_obj_key& obj_key) {
  return tenant_name + (tenant_name.empty() ? "" : ":") + bucket_name + ":" + bucket_id +
    ":" + obj_key.name + ":" + obj_key.instance;
}

static void objexp_get_shard(int shard_num,
                             string *shard)
{
  *shard = objexp_hint_get_shardname(shard_num);
}

static int objexp_hint_parse(CephContext *cct, cls_timeindex_entry &ti_entry,
                             objexp_hint_entry *hint_entry)
{
  try {
    auto iter = ti_entry.value.cbegin();
    decode(*hint_entry, iter);
  } catch (buffer::error& err) {
    ldout(cct, 0) << "ERROR: couldn't decode avail_pools" << dendl;
  }

  return 0;
}

int RGWObjExpStore::objexp_hint_add(const ceph::real_time& delete_at,
                              const string& tenant_name,
                              const string& bucket_name,
                              const string& bucket_id,
                              const rgw_obj_index_key& obj_key)
{
  const string keyext = objexp_hint_get_keyext(tenant_name, bucket_name,
          bucket_id, obj_key);
  objexp_hint_entry he = {
      .tenant = tenant_name,
      .bucket_name = bucket_name,
      .bucket_id = bucket_id,
      .obj_key = obj_key,
      .exp_time = delete_at };
  bufferlist hebl;
  encode(he, hebl);
  librados::ObjectWriteOperation op;
  cls_timeindex_add(op, utime_t(delete_at), keyext, hebl);

  string shard_name = objexp_hint_get_shardname(objexp_key_shard(obj_key, cct->_conf->rgw_objexp_hints_num_shards));
  auto obj = rados_svc->obj(rgw_raw_obj(zone_svc->get_zone_params().log_pool, shard_name));
  int r = obj.open();
  if (r < 0) {
    ldout(cct, 0) << "ERROR: " << __func__ << "(): failed to open obj=" << obj << " (r=" << r << ")" << dendl;
    return r;
  }
  return obj.operate(&op, null_yield);
}

int RGWObjExpStore::objexp_hint_list(const string& oid,
                               const ceph::real_time& start_time,
                               const ceph::real_time& end_time,
                               const int max_entries,
                               const string& marker,
                               list<cls_timeindex_entry>& entries, /* out */
                               string *out_marker,                 /* out */
                               bool *truncated)                    /* out */
{
  librados::ObjectReadOperation op;
  cls_timeindex_list(op, utime_t(start_time), utime_t(end_time), marker, max_entries, entries,
        out_marker, truncated);

  auto obj = rados_svc->obj(rgw_raw_obj(zone_svc->get_zone_params().log_pool, oid));
  int r = obj.open();
  if (r < 0) {
    ldout(cct, 0) << "ERROR: " << __func__ << "(): failed to open obj=" << obj << " (r=" << r << ")" << dendl;
    return r;
  }
  bufferlist obl;
  int ret = obj.operate(&op, &obl, null_yield);

  if ((ret < 0 ) && (ret != -ENOENT)) {
    return ret;
  }

  if ((ret == -ENOENT) && truncated) {
    *truncated = false;
  }

  return 0;
}

static int cls_timeindex_trim_repeat(rgw_rados_ref ref,
                                const string& oid,
                                const utime_t& from_time,
                                const utime_t& to_time,
                                const string& from_marker,
                                const string& to_marker)
{
  bool done = false;
  do {
    librados::ObjectWriteOperation op;
    cls_timeindex_trim(op, from_time, to_time, from_marker, to_marker);
    int r = rgw_rados_operate(ref.pool.ioctx(), oid, &op, null_yield);
    if (r == -ENODATA)
      done = true;
    else if (r < 0)
      return r;
  } while (!done);

  return 0;
}

int RGWObjExpStore::objexp_hint_trim(const string& oid,
                               const ceph::real_time& start_time,
                               const ceph::real_time& end_time,
                               const string& from_marker,
                               const string& to_marker)
{
  auto obj = rados_svc->obj(rgw_raw_obj(zone_svc->get_zone_params().log_pool, oid));
  int r = obj.open();
  if (r < 0) {
    ldout(cct, 0) << "ERROR: " << __func__ << "(): failed to open obj=" << obj << " (r=" << r << ")" << dendl;
    return r;
  }
  auto& ref = obj.get_ref();
  int ret = cls_timeindex_trim_repeat(ref, oid, utime_t(start_time), utime_t(end_time),
          from_marker, to_marker);
  if ((ret < 0 ) && (ret != -ENOENT)) {
    return ret;
  }

  return 0;
}

int RGWObjectExpirer::init_bucket_info(const string& tenant_name,
                                       const string& bucket_name,
                                       const string& bucket_id,
                                       RGWBucketInfo& bucket_info)
{
  /*
   * XXX Here's where it gets tricky. We went to all the trouble of
   * punching the tenant through the objexp_hint_entry, but now we
   * find that our instances do not actually have tenants. They are
   * unique thanks to IDs. So the tenant string is not needed...

   * XXX reloaded: it turns out tenants were needed after all since bucket ids
   * are ephemeral, good call encoding tenant info!
   */

  return store->getRados()->get_bucket_info(store->svc(), tenant_name, bucket_name,
				bucket_info, nullptr, null_yield, nullptr);

}

int RGWObjectExpirer::garbage_single_object(objexp_hint_entry& hint)
{
  RGWBucketInfo bucket_info;

  int ret = init_bucket_info(hint.tenant, hint.bucket_name,
          hint.bucket_id, bucket_info);
  if (-ENOENT == ret) {
    ldout(store->ctx(), 15) << "NOTICE: cannot find bucket = " \
        << hint.bucket_name << ". The object must be already removed" << dendl;
    return -ERR_PRECONDITION_FAILED;
  } else if (ret < 0) {
    ldout(store->ctx(),  1) << "ERROR: could not init bucket = " \
        << hint.bucket_name << "due to ret = " << ret << dendl;
    return ret;
  }

  RGWObjectCtx rctx(store);

  rgw_obj_key key = hint.obj_key;
  if (key.instance.empty()) {
    key.instance = "null";
  }

  rgw_obj obj(bucket_info.bucket, key);
  store->getRados()->set_atomic(&rctx, obj);
  ret = store->getRados()->delete_obj(rctx, bucket_info, obj,
          bucket_info.versioning_status(), 0, hint.exp_time);

  return ret;
}

void RGWObjectExpirer::garbage_chunk(list<cls_timeindex_entry>& entries,      /* in  */
                                  bool& need_trim)                         /* out */
{
  need_trim = false;

  for (list<cls_timeindex_entry>::iterator iter = entries.begin();
       iter != entries.end();
       ++iter)
  {
    objexp_hint_entry hint;
    ldout(store->ctx(), 15) << "got removal hint for: " << iter->key_ts.sec() \
        << " - " << iter->key_ext << dendl;

    int ret = objexp_hint_parse(store->getRados()->ctx(), *iter, &hint);
    if (ret < 0) {
      ldout(store->ctx(), 1) << "cannot parse removal hint for " << hint.obj_key << dendl;
      continue;
    }

    /* PRECOND_FAILED simply means that our hint is not valid.
     * We can silently ignore that and move forward. */
    ret = garbage_single_object(hint);
    if (ret == -ERR_PRECONDITION_FAILED) {
      ldout(store->ctx(), 15) << "not actual hint for object: " << hint.obj_key << dendl;
    } else if (ret < 0) {
      ldout(store->ctx(), 1) << "cannot remove expired object: " << hint.obj_key << dendl;
    }

    need_trim = true;
  }

  return;
}

void RGWObjectExpirer::trim_chunk(const string& shard,
                                  const utime_t& from,
                                  const utime_t& to,
                                  const string& from_marker,
                                  const string& to_marker)
{
  ldout(store->ctx(), 20) << "trying to trim removal hints to=" << to
                          << ", to_marker=" << to_marker << dendl;

  real_time rt_from = from.to_real_time();
  real_time rt_to = to.to_real_time();

  int ret = exp_store.objexp_hint_trim(shard, rt_from, rt_to,
                                       from_marker, to_marker);
  if (ret < 0) {
    ldout(store->ctx(), 0) << "ERROR during trim: " << ret << dendl;
  }

  return;
}

bool RGWObjectExpirer::process_single_shard(const string& shard,
                                            const utime_t& last_run,
                                            const utime_t& round_start)
{
  string marker;
  string out_marker;
  bool truncated = false;
  bool done = true;

  CephContext *cct = store->ctx();
  int num_entries = cct->_conf->rgw_objexp_chunk_size;

  int max_secs = cct->_conf->rgw_objexp_gc_interval;
  utime_t end = ceph_clock_now();
  end += max_secs;

  rados::cls::lock::Lock l(objexp_lock_name);

  utime_t time(max_secs, 0);
  l.set_duration(time);

  int ret = l.lock_exclusive(&store->getRados()->objexp_pool_ctx, shard);
  if (ret == -EBUSY) { /* already locked by another processor */
    dout(5) << __func__ << "(): failed to acquire lock on " << shard << dendl;
    return false;
  }

  do {
    real_time rt_last = last_run.to_real_time();
    real_time rt_start = round_start.to_real_time();

    list<cls_timeindex_entry> entries;
    ret = exp_store.objexp_hint_list(shard, rt_last, rt_start,
                                     num_entries, marker, entries,
                                     &out_marker, &truncated);
    if (ret < 0) {
      ldout(cct, 10) << "cannot get removal hints from shard: " << shard
                     << dendl;
      continue;
    }

    bool need_trim;
    garbage_chunk(entries, need_trim);

    if (need_trim) {
      trim_chunk(shard, last_run, round_start, marker, out_marker);
    }

    utime_t now = ceph_clock_now();
    if (now >= end) {
      done = false;
      break;
    }

    marker = out_marker;
  } while (truncated);

  l.unlock(&store->getRados()->objexp_pool_ctx, shard);
  return done;
}

/* Returns true if all shards have been processed successfully. */
bool RGWObjectExpirer::inspect_all_shards(const utime_t& last_run,
                                          const utime_t& round_start)
{
  CephContext * const cct = store->ctx();
  int num_shards = cct->_conf->rgw_objexp_hints_num_shards;
  bool all_done = true;

  for (int i = 0; i < num_shards; i++) {
    string shard;
    objexp_get_shard(i, &shard);

    ldout(store->ctx(), 20) << "processing shard = " << shard << dendl;

    if (! process_single_shard(shard, last_run, round_start)) {
      all_done = false;
    }
  }

  return all_done;
}

bool RGWObjectExpirer::going_down()
{
  return down_flag;
}

void RGWObjectExpirer::start_processor()
{
  worker = new OEWorker(store->ctx(), this);
  worker->create("rgw_obj_expirer");
}

void RGWObjectExpirer::stop_processor()
{
  down_flag = true;
  if (worker) {
    worker->stop();
    worker->join();
  }
  delete worker;
  worker = NULL;
}

void *RGWObjectExpirer::OEWorker::entry() {
  utime_t last_run;
  do {
    utime_t start = ceph_clock_now();
    ldout(cct, 2) << "object expiration: start" << dendl;
    if (oe->inspect_all_shards(last_run, start)) {
      /* All shards have been processed properly. Next time we can start
       * from this moment. */
      last_run = start;
    }
    ldout(cct, 2) << "object expiration: stop" << dendl;


    if (oe->going_down())
      break;

    utime_t end = ceph_clock_now();
    end -= start;
    int secs = cct->_conf->rgw_objexp_gc_interval;

    if (secs <= end.sec())
      continue; // next round

    secs -= end.sec();

    std::unique_lock l{lock};
    cond.wait_for(l, std::chrono::seconds(secs));
  } while (!oe->going_down());

  return NULL;
}

void RGWObjectExpirer::OEWorker::stop()
{
  std::lock_guard l{lock};
  cond.notify_all();
}

