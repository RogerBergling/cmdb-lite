#ifndef CMDB_AUDIT_H
#define CMDB_AUDIT_H

#include "cmdb/db.h"
#include "cmdb/error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  long long id;
  char ts[21];
  char actor[64];
  char action[64];
  char entity_type[64];
  char entity_id[128];
  char *detail;
  char prev_hash[65];
  char hash[65];
} cmdb_audit_entry_t;

int cmdb_audit_add(cmdb_db_t *db, const char *actor, const char *action,
                   const char *entity_type, const char *entity_id,
                   const char *detail_json, cmdb_error_t *err);

int cmdb_audit_verify(cmdb_db_t *db, int *valid, cmdb_error_t *err);
int cmdb_audit_count(cmdb_db_t *db, size_t *out, cmdb_error_t *err);
int cmdb_audit_list(cmdb_db_t *db, size_t limit, size_t offset,
                    cmdb_audit_entry_t **out, size_t *count, size_t *total,
                    cmdb_error_t *err);
void cmdb_audit_entries_free(cmdb_audit_entry_t *entries, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_AUDIT_H */
