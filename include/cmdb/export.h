#ifndef CMDB_EXPORT_H
#define CMDB_EXPORT_H

#include "cmdb/db.h"
#include "cmdb/error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CMDB_EXPORT_CI_CSV = 0,
  CMDB_EXPORT_REL_CSV,
  CMDB_EXPORT_JSON
} cmdb_export_kind_t;

typedef struct {
  const char *ssh_username;
  int ssh_port;
  const char *ssh_auth_type;
  const char *ssh_key_path;
} cmdb_hibp_export_options_t;

typedef struct {
  size_t exported;
  size_t skipped;
} cmdb_hibp_export_stats_t;

int cmdb_export_save(cmdb_db_t *db, cmdb_export_kind_t kind,
                     const char *path, cmdb_error_t *err);

/*
 * Writes a HIBP-compatible CSV feed for Linux CIs. The feed contains no
 * secrets. SSH defaults are used when a CI does not provide ssh_* attributes.
 */
int cmdb_export_hibp_csv(cmdb_db_t *db, const char *path,
                         const cmdb_hibp_export_options_t *options,
                         cmdb_hibp_export_stats_t *stats,
                         cmdb_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_EXPORT_H */
