#ifndef CMDB_CI_H
#define CMDB_CI_H

#include "cmdb/db.h"
#include "cmdb/error.h"
#include "cmdb/limits.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char key[CMDB_MAX_FIELD_KEY];
  char value[CMDB_MAX_VALUE];
} cmdb_attr_t;

typedef struct {
  char id[37];
  char name[CMDB_MAX_NAME];
  char type_id[37];
  char type_name[CMDB_MAX_TYPE_NAME];
  char description[CMDB_MAX_DESCRIPTION];
  char environment[64];
  char status[64];
  char business_service[CMDB_MAX_NAME];
  char technical_owner[CMDB_MAX_NAME];
  char support_team[CMDB_MAX_NAME];
  char criticality[32];
  char hosting_location[CMDB_MAX_NAME];
  char external_id[CMDB_MAX_EXTERNAL_ID];
  char created_at[21];
  char updated_at[21];
  cmdb_attr_t *attrs;
  size_t attr_count;
} cmdb_ci_t;

typedef struct {
  const char *key;
  const char *value;
} cmdb_attr_input_t;

typedef struct {
  const char *id;
  const char *name;
  const char *type_id;
  const char *description;
  const char *environment;
  const char *status;
  const char *business_service;
  const char *technical_owner;
  const char *support_team;
  const char *criticality;
  const char *hosting_location;
  const char *external_id;
  const cmdb_attr_input_t *attrs;
  size_t attr_count;
} cmdb_ci_input_t;

typedef struct {
  const char *query;
  const char *type_id;
  size_t limit;
  size_t offset;
} cmdb_ci_filter_t;

int cmdb_ci_create(cmdb_db_t *db, const cmdb_ci_input_t *input,
                   char out_id[37], cmdb_error_t *err);
int cmdb_ci_update(cmdb_db_t *db, const char *id, const cmdb_ci_input_t *input,
                   cmdb_error_t *err);
int cmdb_ci_delete(cmdb_db_t *db, const char *id, cmdb_error_t *err);

int cmdb_ci_get(cmdb_db_t *db, const char *id, cmdb_ci_t *out,
                cmdb_error_t *err);
int cmdb_ci_list(cmdb_db_t *db, const cmdb_ci_filter_t *filter,
                 cmdb_ci_t **out, size_t *count, size_t *total,
                 cmdb_error_t *err);
void cmdb_ci_free(cmdb_ci_t *ci);
void cmdb_ci_free_list(cmdb_ci_t *cis, size_t count);
int cmdb_ci_count(cmdb_db_t *db, size_t *out, cmdb_error_t *err);

int cmdb_ci_find_by_external_id(cmdb_db_t *db, const char *external_id,
                               char out_id[37], cmdb_error_t *err);
int cmdb_ci_find_by_name_unique(cmdb_db_t *db, const char *name,
                               char out_id[37], cmdb_error_t *err);
int cmdb_ci_resolve(cmdb_db_t *db, const char *token, char out_id[37],
                    cmdb_error_t *err);

int cmdb_ci_validate_fields(const cmdb_ci_input_t *input, cmdb_error_t *err);
int cmdb_ci_validate(cmdb_db_t *db, const cmdb_ci_input_t *input,
                     cmdb_error_t *err);
int cmdb_ci_exists(cmdb_db_t *db, const char *id, int *exists,
                   cmdb_error_t *err);
int cmdb_ci_create_ex(cmdb_db_t *db, const cmdb_ci_input_t *input,
                      int write_audit, char out_id[37], cmdb_error_t *err);
int cmdb_ci_update_ex(cmdb_db_t *db, const char *id,
                      const cmdb_ci_input_t *input, int write_audit,
                      cmdb_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_CI_H */
