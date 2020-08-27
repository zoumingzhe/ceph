// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab ft=cpp

#pragma once

#include "common/static_ptr.h"

#include "rgw/rgw_service.h"

#include "svc_rados.h"
#include "svc_sys_obj_types.h"
#include "svc_sys_obj_core_types.h"


class RGWSI_Zone;
class RGWSI_SysObj;
class RGWSysObjectCtx;

struct rgw_cache_entry_info;

class RGWSI_SysObj : public RGWServiceInstance
{
  friend struct RGWServices_Def;

public:
  class Obj {
    friend class ROp;

    RGWSI_SysObj_Core *core_svc;
    RGWSysObjectCtx& ctx;
    rgw_raw_obj obj;

  public:
    Obj(RGWSI_SysObj_Core *_core_svc,
        RGWSysObjectCtx& _ctx,
        const rgw_raw_obj& _obj) : core_svc(_core_svc),
                                   ctx(_ctx),
                                   obj(_obj) {}

    void invalidate();

    RGWSysObjectCtx& get_ctx() {
      return ctx;
    }

    rgw_raw_obj& get_obj() {
      return obj;
    }

    struct ROp {
      Obj& source;

      ceph::static_ptr<RGWSI_SysObj_Obj_GetObjState, sizeof(RGWSI_SysObj_Core_GetObjState)> state;
      
      RGWObjVersionTracker *objv_tracker{nullptr};
      map<string, bufferlist> *attrs{nullptr};
      bool raw_attrs{false};
      boost::optional<obj_version> refresh_version{boost::none};
      ceph::real_time *lastmod{nullptr};
      uint64_t *obj_size{nullptr};
      rgw_cache_entry_info *cache_info{nullptr};

      ROp& set_objv_tracker(RGWObjVersionTracker *_objv_tracker) {
        objv_tracker = _objv_tracker;
        return *this;
      }

      ROp& set_last_mod(ceph::real_time *_lastmod) {
        lastmod = _lastmod;
        return *this;
      }

      ROp& set_obj_size(uint64_t *_obj_size) {
        obj_size = _obj_size;
        return *this;
      }

      ROp& set_attrs(map<string, bufferlist> *_attrs) {
        attrs = _attrs;
        return *this;
      }

      ROp& set_raw_attrs(bool ra) {
	raw_attrs = ra;
	return *this;
      }

      ROp& set_refresh_version(boost::optional<obj_version>& rf) {
        refresh_version = rf;
        return *this;
      }

      ROp& set_cache_info(rgw_cache_entry_info *ci) {
        cache_info = ci;
        return *this;
      }

      ROp(Obj& _source);

      int stat(optional_yield y);
      int read(int64_t ofs, int64_t end, bufferlist *pbl, optional_yield y);
      int read(bufferlist *pbl, optional_yield y) {
        return read(0, -1, pbl, y);
      }
      int get_attr(const char *name, bufferlist *dest, optional_yield y);
    };

    struct WOp {
      Obj& source;

      RGWObjVersionTracker *objv_tracker{nullptr};
      map<string, bufferlist> attrs;
      ceph::real_time mtime;
      ceph::real_time *pmtime{nullptr};
      bool exclusive{false};

      WOp& set_objv_tracker(RGWObjVersionTracker *_objv_tracker) {
        objv_tracker = _objv_tracker;
        return *this;
      }

      WOp& set_attrs(map<string, bufferlist>& _attrs) {
        attrs = _attrs;
        return *this;
      }

      WOp& set_attrs(map<string, bufferlist>&& _attrs) {
        attrs = _attrs;
        return *this;
      }

      WOp& set_mtime(const ceph::real_time& _mtime) {
        mtime = _mtime;
        return *this;
      }

      WOp& set_pmtime(ceph::real_time *_pmtime) {
        pmtime = _pmtime;
        return *this;
      }

      WOp& set_exclusive(bool _exclusive = true) {
        exclusive = _exclusive;
        return *this;
      }

      WOp(Obj& _source) : source(_source) {}

      int remove(optional_yield y);
      int write(bufferlist& bl, optional_yield y);

      int write_data(bufferlist& bl, optional_yield y); /* write data only */
      int write_attrs(optional_yield y); /* write attrs only */
      int write_attr(const char *name, bufferlist& bl,
                     optional_yield y); /* write attrs only */
    };

    struct OmapOp {
      Obj& source;

      bool must_exist{false};

      OmapOp& set_must_exist(bool _must_exist = true) {
        must_exist = _must_exist;
        return *this;
      }

      OmapOp(Obj& _source) : source(_source) {}

      int get_all(std::map<string, bufferlist> *m, optional_yield y);
      int get_vals(const string& marker, uint64_t count,
                   std::map<string, bufferlist> *m,
                   bool *pmore, optional_yield y);
      int set(const std::string& key, bufferlist& bl, optional_yield y);
      int set(const map<std::string, bufferlist>& m, optional_yield y);
      int del(const std::string& key, optional_yield y);
    };

    struct WNOp {
      Obj& source;

      WNOp(Obj& _source) : source(_source) {}

      int notify(bufferlist& bl, uint64_t timeout_ms, bufferlist *pbl,
                 optional_yield y);
    };
    ROp rop() {
      return ROp(*this);
    }

    WOp wop() {
      return WOp(*this);
    }

    OmapOp omap() {
      return OmapOp(*this);
    }

    WNOp wn() {
      return WNOp(*this);
    }
  };

  class Pool {
    friend class Op;
    friend class RGWSI_SysObj_Core;

    RGWSI_SysObj_Core *core_svc;
    rgw_pool pool;

  protected:
    using ListImplInfo = RGWSI_SysObj_Pool_ListInfo;

    struct ListCtx {
      ceph::static_ptr<ListImplInfo, sizeof(RGWSI_SysObj_Core_PoolListImplInfo)> impl; /* update this if creating new backend types */
    };

  public:
    Pool(RGWSI_SysObj_Core *_core_svc,
         const rgw_pool& _pool) : core_svc(_core_svc),
                                  pool(_pool) {}

    rgw_pool& get_pool() {
      return pool;
    }

    struct Op {
      Pool& source;
      ListCtx ctx;

      Op(Pool& _source) : source(_source) {}

      int init(const std::string& marker, const std::string& prefix);
      int get_next(int max, std::vector<string> *oids, bool *is_truncated);
      int get_marker(string *marker);
    };

    int list_prefixed_objs(const std::string& prefix, std::function<void(const string&)> cb);

    template <typename Container>
    int list_prefixed_objs(const string& prefix,
                           Container *result) {
      return list_prefixed_objs(prefix, [&](const string& val) {
        result->push_back(val);
      });
    }

    Op op() {
      return Op(*this);
    }
  };

  friend class Obj;
  friend class Obj::ROp;
  friend class Obj::WOp;
  friend class Pool;
  friend class Pool::Op;

protected:
  RGWSI_RADOS *rados_svc{nullptr};
  RGWSI_SysObj_Core *core_svc{nullptr};

  void init(RGWSI_RADOS *_rados_svc,
            RGWSI_SysObj_Core *_core_svc) {
    rados_svc = _rados_svc;
    core_svc = _core_svc;
  }

public:
  RGWSI_SysObj(CephContext *cct): RGWServiceInstance(cct) {}

  RGWSysObjectCtx init_obj_ctx();
  Obj get_obj(RGWSysObjectCtx& obj_ctx, const rgw_raw_obj& obj);

  Pool get_pool(const rgw_pool& pool) {
    return Pool(core_svc, pool);
  }

  RGWSI_Zone *get_zone_svc();
};

using RGWSysObj = RGWSI_SysObj::Obj;

class RGWSysObjectCtx : public RGWSysObjectCtxBase
{
  RGWSI_SysObj *sysobj_svc;
public:
  RGWSysObjectCtx(RGWSI_SysObj *_sysobj_svc) : sysobj_svc(_sysobj_svc) {}

  RGWSI_SysObj::Obj get_obj(const rgw_raw_obj& obj) {
    return sysobj_svc->get_obj(*this, obj);
  }
};
