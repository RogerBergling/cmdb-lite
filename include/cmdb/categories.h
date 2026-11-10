#ifndef CMDB_CATEGORIES_H
#define CMDB_CATEGORIES_H

#include "cmdb/db.h"
#include "cmdb/error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char id[37];
  char name[64];
  char description[256];
  char color[16];
  int sort_order;
  char created_at[21];
  char updated_at[21];
} cmdb_category_t;

int cmdb_categories_list(cmdb_db_t *db, cmdb_category_t **out,
                         size_t *count, cmdb_error_t *err);
int cmdb_categories_get(cmdb_db_t *db, const char *id, cmdb_category_t *out,
                        cmdb_error_t *err);
int cmdb_categories_find_by_name(cmdb_db_t *db, const char *name,
                                 cmdb_category_t *out, cmdb_error_t *err);
int cmdb_categories_create(cmdb_db_t *db, const char *name,
                           const char *description, const char *color,
                           int sort_order, char out_id[37],
                           cmdb_error_t *err);
int cmdb_categories_update(cmdb_db_t *db, const char *id, const char *name,
                           const char *description, const char *color,
                           int sort_order, cmdb_error_t *err);
int cmdb_categories_delete(cmdb_db_t *db, const char *id,
                           cmdb_error_t *err);
int cmdb_categories_exists(cmdb_db_t *db, const char *id);
void cmdb_categories_free(cmdb_category_t *categories);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_CATEGORIES_H */
