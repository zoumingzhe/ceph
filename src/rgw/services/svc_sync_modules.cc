// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab ft=cpp

#include "svc_sync_modules.h"
#include "svc_zone.h"

#include "rgw/rgw_sync_module.h"
#include "rgw/rgw_zone.h"

#define dout_subsys ceph_subsys_rgw

void RGWSI_SyncModules::init(RGWSI_Zone *zone_svc)
{
  svc.zone = zone_svc;
  sync_modules_manager = new RGWSyncModulesManager();
  rgw_register_sync_modules(sync_modules_manager);
}

int RGWSI_SyncModules::do_start()
{
  auto& zone_public_config = svc.zone->get_zone();

  int ret = sync_modules_manager->create_instance(cct, zone_public_config.tier_type, svc.zone->get_zone_params().tier_config, &sync_module);
  if (ret < 0) {
    lderr(cct) << "ERROR: failed to start sync module instance, ret=" << ret << dendl;
    if (ret == -ENOENT) {
      lderr(cct) << "ERROR: " << zone_public_config.tier_type 
        << " sync module does not exist. valid sync modules: " 
        << sync_modules_manager->get_registered_module_names()
        << dendl;
    }
    return ret;
  }

  ldout(cct, 20) << "started sync module instance, tier type = " << zone_public_config.tier_type << dendl;

  return 0;
}

RGWSI_SyncModules::~RGWSI_SyncModules()
{
  delete sync_modules_manager;
}

