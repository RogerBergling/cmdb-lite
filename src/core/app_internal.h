#ifndef CMDB_APP_INTERNAL_H
#define CMDB_APP_INTERNAL_H

#include "cmdb/api.h"
#include "cmdb/db.h"
#include "cmdb/scan_manager.h"

#include "cJSON.h"

#include "app_internal.h"
#include "cmdb/scan_manager.h"

struct cmdb_app {
  cmdb_db_t *db;
  cmdb_api_host_t host;
  cmdb_scan_manager_t *scan_manager;
};

cJSON *cmdb_api_handle(cmdb_app_t *app, cJSON *request);

#endif /* CMDB_APP_INTERNAL_H */
