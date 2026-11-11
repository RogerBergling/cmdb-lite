#ifndef CMDB_RELATIONSHIPS_H
#define CMDB_RELATIONSHIPS_H

#include "cmdb/db.h"
#include "cmdb/error.h"
#include "cmdb/limits.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char id[37];
  char name[CMDB_MAX_RELATIONSHIP_NAME];
  char inverse_name[CMDB_MAX_RELATIONSHIP_NAME];
  char description[512];
  char color[16];
  char default_impact[16];
} cmdb_relationship_type_t;

typedef struct {
  char id[37];
  char source_id[37];
  char source_name[CMDB_MAX_NAME];
  char target_id[37];
  char target_name[CMDB_MAX_NAME];
  char type_id[37];
  char type_name[CMDB_MAX_RELATIONSHIP_NAME];
  char description[1024];
  char impact[16];
  char created_at[21];
  char updated_at[21];
} cmdb_relationship_t;

int cmdb_relationship_types_list(cmdb_db_t *db,
                                 cmdb_relationship_type_t **out,
                                 size_t *count, cmdb_error_t *err);
void cmdb_relationship_types_free(cmdb_relationship_type_t *types);
int cmdb_relationship_type_find(cmdb_db_t *db, const char *name,
                                cmdb_relationship_type_t *out,
                                cmdb_error_t *err);
int cmdb_relationship_type_get(cmdb_db_t *db, const char *id,
                               cmdb_relationship_type_t *out,
                               cmdb_error_t *err);

int cmdb_relationship_impact_valid(const char *impact);

int cmdb_relationship_create(cmdb_db_t *db, const char *source_id,
                             const char *target_id, const char *type_id,
                             const char *description, char out_id[37],
                             cmdb_error_t *err);
int cmdb_relationship_create_ex(cmdb_db_t *db, const char *source_id,
                                const char *target_id, const char *type_id,
                                const char *description, const char *impact,
                                int write_audit, char out_id[37],
                                cmdb_error_t *err);
int cmdb_relationship_set_impact(cmdb_db_t *db, const char *id,
                                 const char *impact, cmdb_error_t *err);
int cmdb_relationship_delete(cmdb_db_t *db, const char *id,
                             cmdb_error_t *err);
int cmdb_relationship_get(cmdb_db_t *db, const char *id,
                          cmdb_relationship_t *out, cmdb_error_t *err);

int cmdb_relationship_list(cmdb_db_t *db, const char *ci_id, size_t limit,
                           size_t offset, cmdb_relationship_t **out,
                           size_t *count, size_t *total, cmdb_error_t *err);
void cmdb_relationship_free(cmdb_relationship_t *rel);
void cmdb_relationship_free_list(cmdb_relationship_t *rels, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_RELATIONSHIPS_H */
