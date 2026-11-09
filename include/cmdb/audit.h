#ifndef CMDB_AUDIT_H
#define CMDB_AUDIT_H

#include "cmdb/db.h"
#include "cmdb/error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int cmdb_audit_add(cmdb_db_t *db, const char *actor, const char *action,
                   const char *entity_type, const char *entity_id,
                   const char *detail_json, cmdb_error_t *err);

int cmdb_audit_verify(cmdb_db_t *db, int *valid, cmdb_error_t *err);
int cmdb_audit_count(cmdb_db_t *db, size_t *out, cmdb_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_AUDIT_H */
