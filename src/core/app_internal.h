#ifndef CMDB_APP_INTERNAL_H
#define CMDB_APP_INTERNAL_H

#include "cmdb/api.h"
#include "cmdb/db.h"

#include "cJSON.h"

struct cmdb_app {
  cmdb_db_t *db;
  cmdb_api_host_t host;
};

cJSON *cmdb_api_handle(cmdb_app_t *app, cJSON *request);

#endif /* CMDB_APP_INTERNAL_H */
