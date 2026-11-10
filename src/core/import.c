#include "cmdb/import.h"

#include "cmdb/audit.h"
#include "cmdb/ci.h"
#include "cmdb/json_util.h"
#include "cmdb/relationships.h"
#include "cmdb/types.h"
#include "cmdb/util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int ascii_casecmp(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    int ca = tolower((unsigned char)*a);
    int cb = tolower((unsigned char)*b);
    if (ca != cb) {
      return ca - cb;
    }
    a++;
    b++;
  }
  return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static void csv_row_free(cmdb_csv_row_t *row) {
  size_t i;
  if (row == NULL) {
    return;
  }
  for (i = 0; i < row->count; i++) {
    free(row->cells[i]);
  }
  free(row->cells);
  row->cells = NULL;
  row->count = 0;
}

static int csv_row_add_field(cmdb_csv_row_t *row, char *value) {
  if (row->count >= CMDB_MAX_IMPORT_COLUMNS) {
    free(value);
    return -1;
  }
  row->cells = cmdb_realloc(row->cells, (row->count + 1) * sizeof(char *));
  row->cells[row->count++] = value;
  return 0;
}

static int csv_row_is_empty(const cmdb_csv_row_t *row) {
  size_t i;
  for (i = 0; i < row->count; i++) {
    if (row->cells[i] != NULL && row->cells[i][0] != '\0') {
      return 0;
    }
  }
  return 1;
}

static int csv_table_add_row(cmdb_csv_table_t *table, cmdb_csv_row_t *row) {
  if (table->row_count >= CMDB_MAX_IMPORT_ROWS + 1) {
    return -1;
  }
  table->rows = cmdb_realloc(table->rows,
                             (table->row_count + 1) * sizeof(*table->rows));
  table->rows[table->row_count++] = *row;
  row->cells = NULL;
  row->count = 0;
  return 0;
}

static int csv_end_field(cmdb_strbuf_t *field, cmdb_csv_row_t *row) {
  char *value = cmdb_strndup(field->data, field->len);
  field->len = 0;
  field->data[0] = '\0';
  return csv_row_add_field(row, value);
}

static int csv_end_row(cmdb_csv_table_t *table, cmdb_csv_row_t *row) {
  if (row->count == 0) {
    return 0;
  }
  if (!csv_row_is_empty(row)) {
    if (csv_table_add_row(table, row) != 0) {
      return -1;
    }
  } else {
    csv_row_free(row);
  }
  row->line = 0;
  return 0;
}

static int normalize_header(char *header) {
  char *p;

  cmdb_trim_in_place(header);
  cmdb_ascii_lower(header);
  p = header;
  while (*p != '\0') {
    if (*p == ' ' || *p == '-') {
      *p = '_';
    }
    p++;
  }
  return header[0] != '\0' ? 0 : -1;
}

int cmdb_csv_parse(const char *data, size_t len, char delimiter,
                   cmdb_csv_table_t *out, cmdb_error_t *err) {
  size_t i = 0;
  size_t line = 1;
  size_t row_line = 1;
  int in_quotes = 0;
  int after_quote = 0;
  int field_started = 0;
  cmdb_strbuf_t field;
  cmdb_csv_row_t row;
  int rc = -1;

  if (data == NULL || len == 0 || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Empty import data");
    return -1;
  }
  if (delimiter == '\0' || delimiter == '"' ||
      delimiter == '\r' || delimiter == '\n') {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CSV delimiter");
    return -1;
  }

  memset(out, 0, sizeof(*out));
  memset(&row, 0, sizeof(row));
  row.line = row_line;
  cmdb_strbuf_init(&field);

  if (len >= 3 && (unsigned char)data[0] == 0xEF &&
      (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
    i = 3;
  }

  while (i < len) {
    unsigned char c = (unsigned char)data[i];

    if (c == '\0') {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "Import contains a NUL byte");
      err->line = (int)line;
      goto cleanup;
    }

    if (in_quotes) {
      if (c == '"') {
        if (i + 1 < len && data[i + 1] == '"') {
          if (field.len >= CMDB_MAX_IMPORT_FIELD) {
            cmdb_error_set(err, CMDB_ERR_LIMIT, "CSV field is too large");
            err->line = (int)line;
            goto cleanup;
          }
          cmdb_strbuf_append_n(&field, "\"", 1);
          i += 2;
        } else {
          in_quotes = 0;
          after_quote = 1;
          i++;
        }
      } else if (c == '\r' || c == '\n') {
        if (field.len >= CMDB_MAX_IMPORT_FIELD) {
          cmdb_error_set(err, CMDB_ERR_LIMIT, "CSV field is too large");
          err->line = (int)line;
          goto cleanup;
        }
        cmdb_strbuf_append_n(&field, "\n", 1);
        if (c == '\r' && i + 1 < len && data[i + 1] == '\n') {
          i += 2;
        } else {
          i++;
        }
        line++;
      } else {
        if (field.len >= CMDB_MAX_IMPORT_FIELD) {
          cmdb_error_set(err, CMDB_ERR_LIMIT, "CSV field is too large");
          err->line = (int)line;
          goto cleanup;
        }
        cmdb_strbuf_append_n(&field, (const char *)&data[i], 1);
        i++;
      }
      continue;
    }

    if (after_quote) {
      if (c == (unsigned char)delimiter) {
        if (csv_end_field(&field, &row) != 0) {
          cmdb_error_set(err, CMDB_ERR_LIMIT, "Too many CSV columns");
          err->line = (int)line;
          goto cleanup;
        }
        after_quote = 0;
        field_started = 0;
        i++;
      } else if (c == '\r' || c == '\n') {
        if (csv_end_field(&field, &row) != 0) {
          cmdb_error_set(err, CMDB_ERR_LIMIT, "Too many CSV columns");
          err->line = (int)line;
          goto cleanup;
        }
        if (csv_end_row(out, &row) != 0) {
          cmdb_error_set(err, CMDB_ERR_LIMIT, "Too many CSV rows");
          err->line = (int)line;
          goto cleanup;
        }
        if (c == '\r' && i + 1 < len && data[i + 1] == '\n') {
          i += 2;
        } else {
          i++;
        }
        line++;
        row_line = line;
        row.line = row_line;
        after_quote = 0;
        field_started = 0;
      } else {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Unexpected character after closing quote");
        err->line = (int)line;
        goto cleanup;
      }
      continue;
    }

    if (c == '"') {
      if (field_started) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Unexpected quote inside an unquoted field");
        err->line = (int)line;
        goto cleanup;
      }
      in_quotes = 1;
      field_started = 1;
      i++;
      continue;
    }

    if (c == (unsigned char)delimiter) {
      if (csv_end_field(&field, &row) != 0) {
        cmdb_error_set(err, CMDB_ERR_LIMIT, "Too many CSV columns");
        err->line = (int)line;
        goto cleanup;
      }
      field_started = 0;
      i++;
      continue;
    }

    if (c == '\r' || c == '\n') {
      if (csv_end_field(&field, &row) != 0) {
        cmdb_error_set(err, CMDB_ERR_LIMIT, "Too many CSV columns");
        err->line = (int)line;
        goto cleanup;
      }
      if (csv_end_row(out, &row) != 0) {
        cmdb_error_set(err, CMDB_ERR_LIMIT, "Too many CSV rows");
        err->line = (int)line;
        goto cleanup;
      }
      if (c == '\r' && i + 1 < len && data[i + 1] == '\n') {
        i += 2;
      } else {
        i++;
      }
      line++;
      row_line = line;
      row.line = row_line;
      field_started = 0;
      continue;
    }

    if (field.len >= CMDB_MAX_IMPORT_FIELD) {
      cmdb_error_set(err, CMDB_ERR_LIMIT, "CSV field is too large");
      err->line = (int)line;
      goto cleanup;
    }
    cmdb_strbuf_append_n(&field, (const char *)&data[i], 1);
    field_started = 1;
    i++;
  }

  if (in_quotes) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Unterminated quoted CSV field");
    err->line = (int)line;
    goto cleanup;
  }

  if (after_quote || field_started || field.len > 0 || row.count > 0) {
    if (csv_end_field(&field, &row) != 0) {
      cmdb_error_set(err, CMDB_ERR_LIMIT, "Too many CSV columns");
      err->line = (int)line;
      goto cleanup;
    }
    if (csv_end_row(out, &row) != 0) {
      cmdb_error_set(err, CMDB_ERR_LIMIT, "Too many CSV rows");
      err->line = (int)line;
      goto cleanup;
    }
  }

  if (out->row_count == 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Import file has no header row");
    goto cleanup;
  }

  out->headers = out->rows[0].cells;
  out->column_count = out->rows[0].count;
  out->rows[0].cells = NULL;
  out->rows[0].count = 0;
  if (out->column_count == 0 || out->column_count > CMDB_MAX_IMPORT_COLUMNS) {
    cmdb_error_set(err, CMDB_ERR_LIMIT, "Invalid CSV header count");
    goto cleanup;
  }

  for (i = 0; i < out->column_count; i++) {
    size_t j;
    if (normalize_header(out->headers[i]) != 0) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "CSV header names must not be empty");
      goto cleanup;
    }
    for (j = 0; j < i; j++) {
      if (strcmp(out->headers[j], out->headers[i]) == 0) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Duplicate CSV header");
        cmdb_error_set_detail(err, "%s", out->headers[i]);
        goto cleanup;
      }
    }
  }

  if (out->row_count > 1) {
    size_t data_rows = out->row_count - 1;
    memmove(out->rows, out->rows + 1, data_rows * sizeof(*out->rows));
    out->row_count = data_rows;
  } else {
    out->row_count = 0;
  }

  for (i = 0; i < out->row_count; i++) {
    cmdb_csv_row_t *data_row = &out->rows[i];
    if (data_row->count > out->column_count) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "CSV row has more columns than the header");
      err->line = (int)data_row->line;
      goto cleanup;
    }
    while (data_row->count < out->column_count) {
      char *empty = cmdb_strdup("");
      if (csv_row_add_field(data_row, empty) != 0) {
        cmdb_error_set(err, CMDB_ERR_LIMIT, "Too many CSV columns");
        err->line = (int)data_row->line;
        goto cleanup;
      }
    }
  }

  cmdb_strbuf_free(&field);
  return 0;

cleanup:
  cmdb_strbuf_free(&field);
  csv_row_free(&row);
  cmdb_csv_free(out);
  return rc;
}

void cmdb_csv_free(cmdb_csv_table_t *table) {
  size_t i;
  if (table == NULL) {
    return;
  }
  if (table->headers != NULL) {
    for (i = 0; i < table->column_count; i++) {
      free(table->headers[i]);
    }
    free(table->headers);
  }
  for (i = 0; i < table->row_count; i++) {
    csv_row_free(&table->rows[i]);
  }
  free(table->rows);
  memset(table, 0, sizeof(*table));
}

int cmdb_csv_column_index(const cmdb_csv_table_t *table, const char *name) {
  size_t i;
  if (table == NULL || name == NULL) {
    return -1;
  }
  for (i = 0; i < table->column_count; i++) {
    if (ascii_casecmp(table->headers[i], name) == 0) {
      return (int)i;
    }
  }
  return -1;
}

static int require_headers(const cmdb_csv_table_t *table,
                           const char *const *names, size_t count,
                           cmdb_error_t *err) {
  size_t i;
  for (i = 0; i < count; i++) {
    if (cmdb_csv_column_index(table, names[i]) < 0) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "Missing required column '%s'", names[i]);
      return -1;
    }
  }
  return 0;
}

static int validate_custom_headers(const cmdb_csv_table_t *table,
                                   cmdb_error_t *err) {
  size_t i;
  for (i = 0; i < table->column_count; i++) {
    if (strncmp(table->headers[i], "custom.", 7) == 0) {
      if (!cmdb_field_key_valid(table->headers[i] + 7)) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Invalid custom column name");
        cmdb_error_set_detail(err, "%s", table->headers[i]);
        return -1;
      }
    }
  }
  return 0;
}

static const char *cell(const cmdb_csv_row_t *row, int index) {
  if (index < 0 || (size_t)index >= row->count) {
    return "";
  }
  return row->cells[index] != NULL ? row->cells[index] : "";
}

static void trim_cell(cmdb_csv_row_t *row, int index) {
  if (index >= 0 && (size_t)index < row->count) {
    cmdb_trim_in_place(row->cells[index]);
  }
}

static void report_init(cmdb_import_report_t *report) {
  memset(report, 0, sizeof(*report));
  report->errors = cmdb_calloc(CMDB_MAX_IMPORT_ERRORS,
                               sizeof(*report->errors));
}

static void report_reset_counts(cmdb_import_report_t *report) {
  report->row_count = 0;
  report->create_count = 0;
  report->update_count = 0;
  report->skip_count = 0;
  report->error_count = 0;
  report->type_create_count = 0;
  report->committed = 0;
  report->error_stored = 0;
}

static void report_add_error(cmdb_import_report_t *report, size_t line,
                             const char *column, const char *message) {
  if (report->error_stored < CMDB_MAX_IMPORT_ERRORS) {
    cmdb_import_error_t *entry = &report->errors[report->error_stored++];
    entry->line = line;
    (void)cmdb_str_copy(entry->column, sizeof(entry->column),
                        column != NULL ? column : "");
    (void)cmdb_str_copy(entry->message, sizeof(entry->message),
                        message != NULL ? message : "Invalid row");
  }
  report->error_count++;
}

void cmdb_import_report_free(cmdb_import_report_t *report) {
  if (report == NULL) {
    return;
  }
  free(report->errors);
  memset(report, 0, sizeof(*report));
}

typedef struct {
  char name[CMDB_MAX_TYPE_NAME];
  char id[37];
  int missing;
  int reported;
} type_cache_entry_t;

typedef struct {
  type_cache_entry_t *items;
  size_t count;
  size_t capacity;
} type_cache_t;

static type_cache_entry_t *type_cache_find(type_cache_t *cache,
                                           const char *name) {
  size_t i;
  for (i = 0; i < cache->count; i++) {
    if (ascii_casecmp(cache->items[i].name, name) == 0) {
      return &cache->items[i];
    }
  }
  return NULL;
}

static type_cache_entry_t *type_cache_add(type_cache_t *cache,
                                          const char *name,
                                          const char *id, int missing) {
  type_cache_entry_t *entry;
  if (cache->count == cache->capacity) {
    cache->capacity = cache->capacity == 0 ? 8 : cache->capacity * 2;
    cache->items = cmdb_realloc(cache->items,
                                cache->capacity * sizeof(*cache->items));
  }
  entry = &cache->items[cache->count++];
  memset(entry, 0, sizeof(*entry));
  (void)cmdb_str_copy(entry->name, sizeof(entry->name), name);
  (void)cmdb_str_copy(entry->id, sizeof(entry->id), id != NULL ? id : "");
  entry->missing = missing;
  entry->reported = 0;
  return entry;
}

static void type_cache_free(type_cache_t *cache) {
  free(cache->items);
  memset(cache, 0, sizeof(*cache));
}

static int resolve_ci_type(cmdb_db_t *db, const char *name,
                           const cmdb_import_options_t *options,
                           int allow_write, type_cache_t *cache,
                           char out_id[37], int *will_create,
                           cmdb_error_t *err) {
  type_cache_entry_t *entry;
  cmdb_ci_type_t type;

  *will_create = 0;
  out_id[0] = '\0';
  if (name == NULL || name[0] == '\0') {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "CI type is required");
    return -1;
  }
  if (strlen(name) >= CMDB_MAX_TYPE_NAME) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "CI type name is too long");
    return -1;
  }

  entry = type_cache_find(cache, name);
  if (entry != NULL) {
    if (entry->missing && !entry->reported) {
      entry->reported = 1;
      *will_create = 1;
    }
    if (entry->id[0] != '\0') {
      (void)cmdb_str_copy(out_id, 37, entry->id);
    }
    return 0;
  }

  if (cmdb_types_find_by_name(db, name, &type, err) == 0) {
    entry = type_cache_add(cache, name, type.id, 0);
    (void)cmdb_str_copy(out_id, 37, type.id);
    (void)entry;
    return 0;
  }
  if (err != NULL && err->code != CMDB_ERR_NOT_FOUND) {
    return -1;
  }
  cmdb_error_clear(err);

  if (!options->create_missing_types) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Unknown CI type '%s' (enable create-missing-types)",
                   name);
    return -1;
  }

  if (!allow_write) {
    entry = type_cache_add(cache, name, "", 1);
    entry->reported = 1;
    *will_create = 1;
    return 0;
  }

  {
    char id[37];
    if (cmdb_types_create(db, name, "Imported CI type", "", "", "",
                          id, err) != 0) {
      return -1;
    }
    entry = type_cache_add(cache, name, id, 1);
    entry->reported = 1;
    *will_create = 1;
    (void)cmdb_str_copy(out_id, 37, id);
  }
  return 0;
}

typedef struct {
  cmdb_ci_input_t input;
  cmdb_attr_input_t *attrs;
  size_t attr_count;
  char id[37];
} ci_row_input_t;

static void ci_row_input_free(ci_row_input_t *row) {
  free(row->attrs);
  row->attrs = NULL;
  row->attr_count = 0;
}

static int build_ci_row_input(const cmdb_csv_table_t *table,
                              cmdb_csv_row_t *row,
                              const char *type_id,
                              ci_row_input_t *out, cmdb_error_t *err) {
  int name_idx = cmdb_csv_column_index(table, "ci_name");
  int ci_id_idx = cmdb_csv_column_index(table, "ci_id");
  int description_idx = cmdb_csv_column_index(table, "description");
  int environment_idx = cmdb_csv_column_index(table, "environment");
  int status_idx = cmdb_csv_column_index(table, "status");
  int business_idx = cmdb_csv_column_index(table, "business_service");
  int owner_idx = cmdb_csv_column_index(table, "technical_owner");
  int support_idx = cmdb_csv_column_index(table, "support_team");
  int criticality_idx = cmdb_csv_column_index(table, "criticality");
  int hosting_idx = cmdb_csv_column_index(table, "hosting_location");
  int external_idx = cmdb_csv_column_index(table, "external_id");
  int last_updated_idx = cmdb_csv_column_index(table, "last_updated");
  size_t i;

  memset(out, 0, sizeof(*out));
  out->input.name = cell(row, name_idx);
  out->input.type_id = type_id;
  out->input.description = cell(row, description_idx);
  out->input.environment = cell(row, environment_idx);
  out->input.status = cell(row, status_idx);
  out->input.business_service = cell(row, business_idx);
  out->input.technical_owner = cell(row, owner_idx);
  out->input.support_team = cell(row, support_idx);
  out->input.criticality = cell(row, criticality_idx);
  out->input.hosting_location = cell(row, hosting_idx);
  out->input.external_id = cell(row, external_idx);

  if (ci_id_idx >= 0) {
    const char *value = cell(row, ci_id_idx);
    if (value[0] != '\0') {
      if (!cmdb_uuid_valid(value)) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "ci_id must be a valid UUID");
        return -1;
      }
      (void)cmdb_str_copy(out->id, sizeof(out->id), value);
      out->input.id = out->id;
    }
  }

  if (last_updated_idx >= 0) {
    const char *value = cell(row, last_updated_idx);
    if (value[0] != '\0' && !cmdb_iso8601_valid(value)) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "last_updated must be ISO 8601 UTC");
      return -1;
    }
  }

  out->attrs = cmdb_calloc(table->column_count, sizeof(*out->attrs));
  for (i = 0; i < table->column_count; i++) {
    if (strncmp(table->headers[i], "custom.", 7) == 0) {
      const char *value = cell(row, (int)i);
      if (value[0] != '\0') {
        out->attrs[out->attr_count].key = table->headers[i] + 7;
        out->attrs[out->attr_count].value = value;
        out->attr_count++;
      }
    }
  }
  out->input.attrs = out->attrs;
  out->input.attr_count = out->attr_count;
  return 0;
}

typedef enum {
  ACTION_CREATE = 0,
  ACTION_UPDATE = 1,
  ACTION_SKIP = 2
} import_action_t;

static int determine_ci_action(cmdb_db_t *db, const cmdb_ci_input_t *input,
                               cmdb_duplicate_policy_t policy,
                               import_action_t *action, char target_id[37],
                               cmdb_error_t *err) {
  int id_exists = 0;
  int ext_exists = 0;
  char ext_from_db[37];

  *action = ACTION_CREATE;
  target_id[0] = '\0';

  if (input->id != NULL && input->id[0] != '\0') {
    if (cmdb_ci_exists(db, input->id, &id_exists, err) != 0) {
      return -1;
    }
  }
  if (input->external_id != NULL && input->external_id[0] != '\0') {
    if (cmdb_ci_find_by_external_id(db, input->external_id,
                                    ext_from_db, err) == 0) {
      ext_exists = 1;
    } else if (err != NULL && err->code != CMDB_ERR_NOT_FOUND) {
      return -1;
    }
    cmdb_error_clear(err);
  }

  if (id_exists && ext_exists && strcmp(input->id, ext_from_db) != 0) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "ci_id and external_id point to different CIs");
    return -1;
  }
  if (!id_exists && !ext_exists) {
    *action = ACTION_CREATE;
    return 0;
  }

  if (id_exists) {
    (void)cmdb_str_copy(target_id, 37, input->id);
  } else {
    (void)cmdb_str_copy(target_id, 37, ext_from_db);
  }

  if (policy == CMDB_DUPLICATE_ERROR) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "CI already exists (duplicate policy: error)");
    return -1;
  }
  if (policy == CMDB_DUPLICATE_SKIP) {
    *action = ACTION_SKIP;
    return 0;
  }
  *action = ACTION_UPDATE;
  return 0;
}

static void trim_ci_cells(cmdb_csv_row_t *row,
                          const cmdb_csv_table_t *table) {
  int indexes[] = {
    cmdb_csv_column_index(table, "ci_id"),
    cmdb_csv_column_index(table, "ci_name"),
    cmdb_csv_column_index(table, "ci_type"),
    cmdb_csv_column_index(table, "description"),
    cmdb_csv_column_index(table, "environment"),
    cmdb_csv_column_index(table, "status"),
    cmdb_csv_column_index(table, "business_service"),
    cmdb_csv_column_index(table, "technical_owner"),
    cmdb_csv_column_index(table, "support_team"),
    cmdb_csv_column_index(table, "criticality"),
    cmdb_csv_column_index(table, "hosting_location"),
    cmdb_csv_column_index(table, "external_id"),
    cmdb_csv_column_index(table, "last_updated")
  };
  size_t i;
  for (i = 0; i < sizeof(indexes) / sizeof(indexes[0]); i++) {
    trim_cell(row, indexes[i]);
  }
}

static int validate_attrs_basic(const cmdb_ci_input_t *input,
                                cmdb_error_t *err) {
  size_t i;
  for (i = 0; i < input->attr_count; i++) {
    if (!cmdb_field_key_valid(input->attrs[i].key)) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "Invalid custom field key");
      return -1;
    }
    if (input->attrs[i].value != NULL) {
      size_t len = strlen(input->attrs[i].value);
      if (len >= CMDB_MAX_VALUE) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Custom field value is too long");
        return -1;
      }
      if (!cmdb_utf8_valid(input->attrs[i].value, len)) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Custom field value is not valid UTF-8");
        return -1;
      }
    }
  }
  return 0;
}

int cmdb_import_ci_preview(cmdb_db_t *db, const cmdb_csv_table_t *table,
                           const cmdb_import_options_t *options,
                           cmdb_import_report_t *report, cmdb_error_t *err) {
  static const char *const required[] = {"ci_name", "ci_type"};
  type_cache_t cache;
  size_t i;
  int name_idx;
  int type_idx;
  char placeholder[37];

  if (db == NULL || table == NULL || options == NULL || report == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid import arguments");
    return -1;
  }
  if (cmdb_random_uuid(placeholder) != 0) {
    (void)cmdb_str_copy(placeholder, sizeof(placeholder),
                        "00000000-0000-4000-8000-000000000000");
  }
  report_init(report);
  memset(&cache, 0, sizeof(cache));

  if (require_headers(table, required, 2, err) != 0 ||
      validate_custom_headers(table, err) != 0) {
    type_cache_free(&cache);
    return -1;
  }

  name_idx = cmdb_csv_column_index(table, "ci_name");
  type_idx = cmdb_csv_column_index(table, "ci_type");

  for (i = 0; i < table->row_count; i++) {
    cmdb_csv_row_t *row = &table->rows[i];
    const char *name;
    const char *type_name;
    char type_id[37];
    int will_create = 0;
    ci_row_input_t input;
    import_action_t action;
    char target_id[37];
    cmdb_error_t row_error;

    report->row_count++;
    cmdb_error_clear(&row_error);
    trim_ci_cells(row, table);
    name = cell(row, name_idx);
    type_name = cell(row, type_idx);
    if (name[0] == '\0') {
      report_add_error(report, row->line, "ci_name", "CI name is required");
      continue;
    }
    if (type_name[0] == '\0') {
      report_add_error(report, row->line, "ci_type", "CI type is required");
      continue;
    }

    if (resolve_ci_type(db, type_name, options, 0, &cache,
                        type_id, &will_create, &row_error) != 0) {
      report_add_error(report, row->line, "ci_type", row_error.message);
      continue;
    }
    if (will_create) {
      report->type_create_count++;
    }

    if (build_ci_row_input(table, row,
                           type_id[0] != '\0' ? type_id : placeholder,
                           &input, &row_error) != 0) {
      report_add_error(report, row->line, "", row_error.message);
      ci_row_input_free(&input);
      continue;
    }

    if (type_id[0] == '\0') {
      if (cmdb_ci_validate_fields(&input.input, &row_error) != 0 ||
          validate_attrs_basic(&input.input, &row_error) != 0) {
        report_add_error(report, row->line, "", row_error.message);
        ci_row_input_free(&input);
        continue;
      }
    } else if (cmdb_ci_validate(db, &input.input, &row_error) != 0) {
      report_add_error(report, row->line, "", row_error.message);
      ci_row_input_free(&input);
      continue;
    }

    if (determine_ci_action(db, &input.input, options->duplicate_policy,
                            &action, target_id, &row_error) != 0) {
      report_add_error(report, row->line, "", row_error.message);
      ci_row_input_free(&input);
      continue;
    }
    if (action == ACTION_CREATE) {
      report->create_count++;
    } else if (action == ACTION_UPDATE) {
      report->update_count++;
    } else {
      report->skip_count++;
    }
    ci_row_input_free(&input);
  }

  type_cache_free(&cache);
  return 0;
}

static int audit_import(cmdb_db_t *db, const char *kind,
                        const cmdb_import_report_t *report,
                        cmdb_error_t *err) {
  cJSON *detail = cJSON_CreateObject();
  char *json;
  int rc;

  if (detail == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create audit detail");
    return -1;
  }
  cJSON_AddNumberToObject(detail, "rows", (double)report->row_count);
  cJSON_AddNumberToObject(detail, "created", (double)report->create_count);
  cJSON_AddNumberToObject(detail, "updated", (double)report->update_count);
  cJSON_AddNumberToObject(detail, "skipped", (double)report->skip_count);
  json = cJSON_PrintUnformatted(detail);
  cJSON_Delete(detail);
  if (json == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create audit detail");
    return -1;
  }
  rc = cmdb_audit_add(db, "local", "import", kind, "", json, err);
  free(json);
  return rc;
}

int cmdb_import_ci_commit(cmdb_db_t *db, const cmdb_csv_table_t *table,
                          const cmdb_import_options_t *options,
                          cmdb_import_report_t *report, cmdb_error_t *err) {
  static const char *const required[] = {"ci_name", "ci_type"};
  type_cache_t cache;
  size_t i;
  int name_idx;
  int type_idx;

  if (db == NULL || table == NULL || options == NULL || report == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid import arguments");
    return -1;
  }

  if (cmdb_import_ci_preview(db, table, options, report, err) != 0) {
    return -1;
  }
  if (report->error_count > 0) {
    return 0;
  }
  if (require_headers(table, required, 2, err) != 0) {
    return -1;
  }

  name_idx = cmdb_csv_column_index(table, "ci_name");
  type_idx = cmdb_csv_column_index(table, "ci_type");
  report_reset_counts(report);
  memset(&cache, 0, sizeof(cache));

  if (cmdb_db_begin(db, err) != 0) {
    type_cache_free(&cache);
    return -1;
  }

  for (i = 0; i < table->row_count; i++) {
    cmdb_csv_row_t *row = &table->rows[i];
    const char *name = cell(row, name_idx);
    const char *type_name = cell(row, type_idx);
    char type_id[37];
    int will_create = 0;
    ci_row_input_t input;
    import_action_t action;
    char target_id[37];
    char created_id[37];
    cmdb_error_t row_error;

    report->row_count++;
    cmdb_error_clear(&row_error);
    trim_ci_cells(row, table);
    if (name[0] == '\0' || type_name[0] == '\0') {
      report_add_error(report, row->line, "ci_name",
                       "CI name and type are required");
      goto rollback;
    }
    if (resolve_ci_type(db, type_name, options, 1, &cache,
                        type_id, &will_create, &row_error) != 0) {
      report_add_error(report, row->line, "ci_type", row_error.message);
      goto rollback;
    }
    if (will_create) {
      report->type_create_count++;
    }
    if (type_id[0] == '\0') {
      report_add_error(report, row->line, "ci_type",
                       "CI type could not be resolved");
      goto rollback;
    }
    if (build_ci_row_input(table, row, type_id, &input, &row_error) != 0) {
      report_add_error(report, row->line, "", row_error.message);
      ci_row_input_free(&input);
      goto rollback;
    }
    if (cmdb_ci_validate(db, &input.input, &row_error) != 0) {
      report_add_error(report, row->line, "", row_error.message);
      ci_row_input_free(&input);
      goto rollback;
    }
    if (determine_ci_action(db, &input.input, options->duplicate_policy,
                            &action, target_id, &row_error) != 0) {
      report_add_error(report, row->line, "", row_error.message);
      ci_row_input_free(&input);
      goto rollback;
    }

    if (action == ACTION_CREATE) {
      if (cmdb_ci_create_ex(db, &input.input, 0, created_id, &row_error) != 0) {
        report_add_error(report, row->line, "", row_error.message);
        ci_row_input_free(&input);
        goto rollback;
      }
      report->create_count++;
    } else if (action == ACTION_UPDATE) {
      if (cmdb_ci_update_ex(db, target_id, &input.input, 0, &row_error) != 0) {
        report_add_error(report, row->line, "", row_error.message);
        ci_row_input_free(&input);
        goto rollback;
      }
      report->update_count++;
    } else {
      report->skip_count++;
    }
    ci_row_input_free(&input);
  }

  if (audit_import(db, "cis", report, err) != 0) {
    goto rollback_error;
  }
  if (cmdb_db_commit(db, err) != 0) {
    (void)cmdb_db_rollback(db, NULL);
    type_cache_free(&cache);
    return -1;
  }
  report->committed = 1;
  type_cache_free(&cache);
  return 0;

rollback:
  (void)cmdb_db_rollback(db, NULL);
  type_cache_free(&cache);
  return 0;

rollback_error:
  (void)cmdb_db_rollback(db, NULL);
  type_cache_free(&cache);
  return -1;
}

static int rel_find_existing(cmdb_db_t *db, const char *source_id,
                             const char *target_id, const char *type_id,
                             char out_id[37], int *found, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  *found = 0;
  out_id[0] = '\0';
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT id FROM relationships WHERE source_id = ? "
                         "AND target_id = ? AND type_id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot check relationship");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, source_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, target_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, type_id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    const unsigned char *id = sqlite3_column_text(stmt, 0);
    (void)cmdb_str_copy(out_id, 37, (const char *)id);
    *found = 1;
  } else if (rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot check relationship");
    return -1;
  }
  sqlite3_finalize(stmt);
  return 0;
}

int cmdb_import_rel_preview(cmdb_db_t *db, const cmdb_csv_table_t *table,
                            const cmdb_import_options_t *options,
                            cmdb_import_report_t *report, cmdb_error_t *err) {
  static const char *const required[] = {
    "source_ci", "relationship_type", "target_ci"
  };
  size_t i;
  int source_idx;
  int type_idx;
  int target_idx;
  int description_idx;

  if (db == NULL || table == NULL || options == NULL || report == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid import arguments");
    return -1;
  }
  report_init(report);
  if (require_headers(table, required, 3, err) != 0) {
    return -1;
  }

  source_idx = cmdb_csv_column_index(table, "source_ci");
  type_idx = cmdb_csv_column_index(table, "relationship_type");
  target_idx = cmdb_csv_column_index(table, "target_ci");
  description_idx = cmdb_csv_column_index(table, "description");

  for (i = 0; i < table->row_count; i++) {
    cmdb_csv_row_t *row = &table->rows[i];
    const char *source;
    const char *type_name;
    const char *target;
    const char *description;
    char source_id[37];
    char target_id[37];
    cmdb_relationship_type_t type;
    char existing_id[37];
    int found = 0;
    cmdb_error_t row_error;

    report->row_count++;
    cmdb_error_clear(&row_error);
    trim_cell(row, source_idx);
    trim_cell(row, type_idx);
    trim_cell(row, target_idx);
    trim_cell(row, description_idx);
    source = cell(row, source_idx);
    type_name = cell(row, type_idx);
    target = cell(row, target_idx);
    description = cell(row, description_idx);

    if (source[0] == '\0' || type_name[0] == '\0' || target[0] == '\0') {
      report_add_error(report, row->line, "",
                       "source_ci, relationship_type and target_ci are required");
      continue;
    }
    if (strlen(description) > 1023 ||
        (description[0] != '\0' &&
         (!cmdb_utf8_valid(description, strlen(description)) ||
          cmdb_has_control_chars(description)))) {
      report_add_error(report, row->line, "description",
                       "Invalid relationship description");
      continue;
    }
    if (cmdb_ci_resolve(db, source, source_id, &row_error) != 0) {
      report_add_error(report, row->line, "source_ci", row_error.message);
      continue;
    }
    if (cmdb_ci_resolve(db, target, target_id, &row_error) != 0) {
      report_add_error(report, row->line, "target_ci", row_error.message);
      continue;
    }
    if (strcmp(source_id, target_id) == 0) {
      report_add_error(report, row->line, "",
                       "A CI cannot have a relationship to itself");
      continue;
    }
    if (cmdb_relationship_type_find(db, type_name, &type, &row_error) != 0) {
      report_add_error(report, row->line, "relationship_type",
                       row_error.message);
      continue;
    }
    if (rel_find_existing(db, source_id, target_id, type.id,
                          existing_id, &found, &row_error) != 0) {
      report_add_error(report, row->line, "", row_error.message);
      continue;
    }
    if (found) {
      if (options->duplicate_policy == CMDB_DUPLICATE_ERROR) {
        report_add_error(report, row->line, "",
                         "Relationship already exists (duplicate policy: error)");
      } else if (options->duplicate_policy == CMDB_DUPLICATE_SKIP) {
        report->skip_count++;
      } else {
        report->update_count++;
      }
    } else {
      report->create_count++;
    }
  }
  return 0;
}

static int update_relationship_description(cmdb_db_t *db, const char *id,
                                           const char *description,
                                           cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char now[21];
  int rc;

  cmdb_iso8601_now(now);
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "UPDATE relationships SET description = ?, "
                         "updated_at = ? WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare relationship update");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, description != NULL ? description : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot update relationship");
    return -1;
  }
  return 0;
}

int cmdb_import_rel_commit(cmdb_db_t *db, const cmdb_csv_table_t *table,
                           const cmdb_import_options_t *options,
                           cmdb_import_report_t *report, cmdb_error_t *err) {
  static const char *const required[] = {
    "source_ci", "relationship_type", "target_ci"
  };
  size_t i;
  int source_idx;
  int type_idx;
  int target_idx;
  int description_idx;

  if (db == NULL || table == NULL || options == NULL || report == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid import arguments");
    return -1;
  }
  if (cmdb_import_rel_preview(db, table, options, report, err) != 0) {
    return -1;
  }
  if (report->error_count > 0) {
    return 0;
  }
  if (require_headers(table, required, 3, err) != 0) {
    return -1;
  }

  source_idx = cmdb_csv_column_index(table, "source_ci");
  type_idx = cmdb_csv_column_index(table, "relationship_type");
  target_idx = cmdb_csv_column_index(table, "target_ci");
  description_idx = cmdb_csv_column_index(table, "description");
  report_reset_counts(report);

  if (cmdb_db_begin(db, err) != 0) {
    return -1;
  }

  for (i = 0; i < table->row_count; i++) {
    cmdb_csv_row_t *row = &table->rows[i];
    const char *source;
    const char *type_name;
    const char *target;
    const char *description;
    char source_id[37];
    char target_id[37];
    char existing_id[37];
    char created_id[37];
    cmdb_relationship_type_t type;
    int found = 0;
    cmdb_error_t row_error;

    report->row_count++;
    cmdb_error_clear(&row_error);
    trim_cell(row, source_idx);
    trim_cell(row, type_idx);
    trim_cell(row, target_idx);
    trim_cell(row, description_idx);
    source = cell(row, source_idx);
    type_name = cell(row, type_idx);
    target = cell(row, target_idx);
    description = cell(row, description_idx);

    if (source[0] == '\0' || type_name[0] == '\0' || target[0] == '\0') {
      report_add_error(report, row->line, "",
                       "source_ci, relationship_type and target_ci are required");
      goto rollback;
    }
    if (cmdb_ci_resolve(db, source, source_id, &row_error) != 0) {
      report_add_error(report, row->line, "source_ci", row_error.message);
      goto rollback;
    }
    if (cmdb_ci_resolve(db, target, target_id, &row_error) != 0) {
      report_add_error(report, row->line, "target_ci", row_error.message);
      goto rollback;
    }
    if (strcmp(source_id, target_id) == 0) {
      report_add_error(report, row->line, "",
                       "A CI cannot have a relationship to itself");
      goto rollback;
    }
    if (cmdb_relationship_type_find(db, type_name, &type, &row_error) != 0) {
      report_add_error(report, row->line, "relationship_type",
                       row_error.message);
      goto rollback;
    }
    if (rel_find_existing(db, source_id, target_id, type.id,
                          existing_id, &found, &row_error) != 0) {
      report_add_error(report, row->line, "", row_error.message);
      goto rollback;
    }
    if (found) {
      if (options->duplicate_policy == CMDB_DUPLICATE_ERROR) {
        report_add_error(report, row->line, "",
                         "Relationship already exists (duplicate policy: error)");
        goto rollback;
      }
      if (options->duplicate_policy == CMDB_DUPLICATE_SKIP) {
        report->skip_count++;
      } else {
        if (update_relationship_description(db, existing_id,
                                            description, &row_error) != 0) {
          report_add_error(report, row->line, "", row_error.message);
          goto rollback;
        }
        report->update_count++;
      }
    } else {
      if (cmdb_relationship_create_ex(db, source_id, target_id, type.id,
                                      description, 0, created_id,
                                      &row_error) != 0) {
        report_add_error(report, row->line, "", row_error.message);
        goto rollback;
      }
      report->create_count++;
    }
  }

  if (audit_import(db, "relationships", report, err) != 0) {
    goto rollback_error;
  }
  if (cmdb_db_commit(db, err) != 0) {
    (void)cmdb_db_rollback(db, NULL);
    return -1;
  }
  report->committed = 1;
  return 0;

rollback:
  (void)cmdb_db_rollback(db, NULL);
  return 0;

rollback_error:
  (void)cmdb_db_rollback(db, NULL);
  return -1;
}
