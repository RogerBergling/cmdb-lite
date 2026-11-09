#ifndef CMDB_IMPORT_H
#define CMDB_IMPORT_H

#include "cmdb/db.h"
#include "cmdb/error.h"
#include "cmdb/limits.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char **cells;
  size_t count;
  size_t line;
} cmdb_csv_row_t;

typedef struct {
  char **headers;
  size_t column_count;
  cmdb_csv_row_t *rows;
  size_t row_count;
} cmdb_csv_table_t;

typedef enum {
  CMDB_DUPLICATE_ERROR = 0,
  CMDB_DUPLICATE_SKIP = 1,
  CMDB_DUPLICATE_UPDATE = 2
} cmdb_duplicate_policy_t;

typedef struct {
  int create_missing_types;
  cmdb_duplicate_policy_t duplicate_policy;
} cmdb_import_options_t;

typedef struct {
  size_t line;
  char column[CMDB_MAX_FIELD_KEY];
  char message[256];
} cmdb_import_error_t;

typedef struct {
  size_t row_count;
  size_t create_count;
  size_t update_count;
  size_t skip_count;
  size_t error_count;
  size_t type_create_count;
  int committed;
  cmdb_import_error_t *errors;
  size_t error_stored;
} cmdb_import_report_t;

int cmdb_csv_parse(const char *data, size_t len, char delimiter,
                   cmdb_csv_table_t *out, cmdb_error_t *err);
void cmdb_csv_free(cmdb_csv_table_t *table);
int cmdb_csv_column_index(const cmdb_csv_table_t *table, const char *name);

int cmdb_import_ci_preview(cmdb_db_t *db, const cmdb_csv_table_t *table,
                           const cmdb_import_options_t *options,
                           cmdb_import_report_t *report, cmdb_error_t *err);
int cmdb_import_ci_commit(cmdb_db_t *db, const cmdb_csv_table_t *table,
                          const cmdb_import_options_t *options,
                          cmdb_import_report_t *report, cmdb_error_t *err);

int cmdb_import_rel_preview(cmdb_db_t *db, const cmdb_csv_table_t *table,
                            const cmdb_import_options_t *options,
                            cmdb_import_report_t *report, cmdb_error_t *err);
int cmdb_import_rel_commit(cmdb_db_t *db, const cmdb_csv_table_t *table,
                           const cmdb_import_options_t *options,
                           cmdb_import_report_t *report, cmdb_error_t *err);

void cmdb_import_report_free(cmdb_import_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_IMPORT_H */
