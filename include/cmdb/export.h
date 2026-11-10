#ifndef CMDB_EXPORT_H
#define CMDB_EXPORT_H

#include "cmdb/db.h"
#include "cmdb/error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CMDB_EXPORT_CI_CSV = 0,
  CMDB_EXPORT_REL_CSV,
  CMDB_EXPORT_JSON
} cmdb_export_kind_t;

int cmdb_export_save(cmdb_db_t *db, cmdb_export_kind_t kind,
                     const char *path, cmdb_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_EXPORT_H */
