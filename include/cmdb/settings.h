#ifndef CMDB_SETTINGS_H
#define CMDB_SETTINGS_H

#include "cmdb/error.h"
#include "cmdb/limits.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CMDB_MAX_RECENT_DATABASES 8u

typedef struct {
  char last_database[CMDB_MAX_PATH];
  char recent_databases[CMDB_MAX_RECENT_DATABASES][CMDB_MAX_PATH];
  size_t recent_count;
} cmdb_settings_t;

/* Loads the per-user config file. A missing file yields an empty settings set. */
int cmdb_settings_load(cmdb_settings_t *settings, cmdb_error_t *err);
int cmdb_settings_save(const cmdb_settings_t *settings, cmdb_error_t *err);
void cmdb_settings_remember(cmdb_settings_t *settings, const char *path);
char *cmdb_settings_config_path(void);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_SETTINGS_H */
