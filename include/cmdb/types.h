#ifndef CMDB_TYPES_H
#define CMDB_TYPES_H

#include "cmdb/db.h"
#include "cmdb/error.h"
#include "cmdb/limits.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char id[37];
  char name[CMDB_MAX_TYPE_NAME];
  char description[CMDB_MAX_DESCRIPTION];
  char icon[64];
  char color[16];
  char category_id[37];
  char category[64];
  char created_at[21];
  char updated_at[21];
} cmdb_ci_type_t;

typedef struct {
  char id[37];
  char type_id[37];
  char key[CMDB_MAX_FIELD_KEY];
  char label[CMDB_MAX_FIELD_LABEL];
  char field_type[CMDB_MAX_FIELD_TYPE];
  int required;
  char options_json[1024];
  int sort_order;
} cmdb_type_field_t;

typedef struct {
  const char *key;
  const char *label;
  const char *field_type;
  int required;
  const char *options_json;
  int sort_order;
} cmdb_type_field_input_t;

int cmdb_types_list(cmdb_db_t *db, cmdb_ci_type_t **out, size_t *count,
                    cmdb_error_t *err);
void cmdb_types_free(cmdb_ci_type_t *types);

int cmdb_types_get(cmdb_db_t *db, const char *id, cmdb_ci_type_t *out,
                   cmdb_error_t *err);
int cmdb_types_find_by_name(cmdb_db_t *db, const char *name,
                           cmdb_ci_type_t *out, cmdb_error_t *err);

int cmdb_types_create(cmdb_db_t *db, const char *name, const char *description,
                      const char *icon, const char *color,
                      const char *category, char out_id[37],
                      cmdb_error_t *err);
int cmdb_types_update(cmdb_db_t *db, const char *id, const char *name,
                      const char *description, const char *icon,
                      const char *color, const char *category,
                      cmdb_error_t *err);
int cmdb_types_delete(cmdb_db_t *db, const char *id, cmdb_error_t *err);

int cmdb_type_fields_list(cmdb_db_t *db, const char *type_id,
                          cmdb_type_field_t **out, size_t *count,
                          cmdb_error_t *err);
void cmdb_type_fields_free(cmdb_type_field_t *fields);
int cmdb_type_field_get(cmdb_db_t *db, const char *id,
                        cmdb_type_field_t *out, cmdb_error_t *err);
int cmdb_type_field_add(cmdb_db_t *db, const char *type_id,
                        const cmdb_type_field_input_t *input,
                        char out_id[37], cmdb_error_t *err);
int cmdb_type_field_update(cmdb_db_t *db, const char *id,
                           const cmdb_type_field_input_t *input,
                           cmdb_error_t *err);
int cmdb_type_field_delete(cmdb_db_t *db, const char *id,
                           cmdb_error_t *err);

int cmdb_field_key_valid(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_TYPES_H */
