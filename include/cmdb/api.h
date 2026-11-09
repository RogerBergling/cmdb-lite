#ifndef CMDB_API_H
#define CMDB_API_H

#include "cmdb/db.h"
#include "cmdb/error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*cmdb_choose_file_fn)(
  void *userdata, int save, const char *title,
  const char *filter_name, const char *filter_pattern,
  const char *suggested_name, char **path_out, cmdb_error_t *err);

typedef struct {
  void *userdata;
  cmdb_choose_file_fn choose_file;
} cmdb_api_host_t;

typedef struct cmdb_app cmdb_app_t;

cmdb_app_t *cmdb_app_new(const cmdb_api_host_t *host, cmdb_error_t *err);
void cmdb_app_free(cmdb_app_t *app);

char *cmdb_app_handle(cmdb_app_t *app, const char *request,
                      size_t request_len);
cmdb_db_t *cmdb_app_db(cmdb_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_API_H */
