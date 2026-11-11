#ifndef CMDB_SCHEMA_H
#define CMDB_SCHEMA_H

#include "cmdb/db.h"
#include "cmdb/error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CMDB_SCHEMA_VERSION 4

int cmdb_schema_create(cmdb_db_t *db, cmdb_error_t *err);
int cmdb_schema_seed(cmdb_db_t *db, cmdb_error_t *err);
int cmdb_schema_migrate(cmdb_db_t *db, cmdb_error_t *err);
int cmdb_schema_validate(cmdb_db_t *db, cmdb_error_t *err);
int cmdb_schema_meta_get(cmdb_db_t *db, const char *key,
                         char *out, size_t out_size, cmdb_error_t *err);
int cmdb_schema_meta_set(cmdb_db_t *db, const char *key, const char *value,
                         cmdb_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_SCHEMA_H */
