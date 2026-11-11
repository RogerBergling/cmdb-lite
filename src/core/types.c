#include "cmdb/types.h"

#include "cmdb/audit.h"
#include "cmdb/categories.h"
#include "cmdb/json_util.h"
#include "cmdb/util.h"

#include <stdlib.h>
#include <string.h>

static int validate_text(const char *value, size_t max_len, int required,
                         const char *label, cmdb_error_t *err) {
  size_t len;

  if (value == NULL) {
    if (required) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION, "%s is required", label);
      return -1;
    }
    return 0;
  }
  len = strlen(value);
  if (len > max_len) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "%s is too long (maximum %zu bytes)", label, max_len);
    return -1;
  }
  if (!cmdb_utf8_valid(value, len)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "%s is not valid UTF-8", label);
    return -1;
  }
  if (cmdb_has_control_chars(value)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "%s contains control characters", label);
    return -1;
  }
  return 0;
}

int cmdb_field_key_valid(const char *key) {
  size_t i;
  size_t len;

  if (key == NULL) {
    return 0;
  }
  len = strlen(key);
  if (len == 0 || len >= CMDB_MAX_FIELD_KEY) {
    return 0;
  }
  if (!(key[0] >= 'a' && key[0] <= 'z')) {
    return 0;
  }
  for (i = 1; i < len; i++) {
    char c = key[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
      return 0;
    }
  }
  return 1;
}

static int read_type_row(sqlite3_stmt *stmt, cmdb_ci_type_t *out) {
  const unsigned char *text;

  text = sqlite3_column_text(stmt, 0);
  if (cmdb_str_copy(out->id, sizeof(out->id),
                    (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 1);
  if (cmdb_str_copy(out->name, sizeof(out->name),
                    (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 2);
  if (cmdb_str_copy(out->description, sizeof(out->description),
                    (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 3);
  if (cmdb_str_copy(out->icon, sizeof(out->icon),
                    (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 4);
  if (cmdb_str_copy(out->color, sizeof(out->color),
                    (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 5);
  if (cmdb_str_copy(out->category_id, sizeof(out->category_id),
                    (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 6);
  if (cmdb_str_copy(out->category, sizeof(out->category),
                    (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 7);
  if (cmdb_str_copy(out->created_at, sizeof(out->created_at),
                    (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 8);
  if (cmdb_str_copy(out->updated_at, sizeof(out->updated_at),
                    (const char *)text) != 0) {
    return -1;
  }
  return 0;
}

int cmdb_types_list(cmdb_db_t *db, cmdb_ci_type_t **out, size_t *count,
                    cmdb_error_t *err) {
  sqlite3 *handle;
  sqlite3_stmt *stmt = NULL;
  size_t total = 0;
  size_t i = 0;
  cmdb_ci_type_t *types;

  if (db == NULL || out == NULL || count == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid type list arguments");
    return -1;
  }
  *out = NULL;
  *count = 0;
  handle = cmdb_db_sqlite(db);

  if (sqlite3_prepare_v2(handle, "SELECT count(*) FROM ci_types;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot count CI types");
    return -1;
  }
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    total = (size_t)sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);

  if (total == 0) {
    return 0;
  }

  types = cmdb_calloc(total, sizeof(*types));
  if (sqlite3_prepare_v2(handle,
                         "SELECT t.id, t.name, t.description, t.icon, t.color, "
                         "t.category, COALESCE(c.name, ''), t.created_at, "
                         "t.updated_at FROM ci_types t "
                         "LEFT JOIN ci_categories c ON c.id = t.category "
                         "ORDER BY COALESCE(c.sort_order, 100000), "
                         "COALESCE(c.name, '') COLLATE NOCASE, "
                         "t.name COLLATE NOCASE;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    free(types);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list CI types");
    return -1;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW && i < total) {
    if (read_type_row(stmt, &types[i]) != 0) {
      sqlite3_finalize(stmt);
      free(types);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "CI type value is too long");
      return -1;
    }
    i++;
  }
  sqlite3_finalize(stmt);
  *out = types;
  *count = i;
  return 0;
}

void cmdb_types_free(cmdb_ci_type_t *types) {
  free(types);
}

int cmdb_types_get(cmdb_db_t *db, const char *id, cmdb_ci_type_t *out,
                   cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || id == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid type lookup");
    return -1;
  }
  memset(out, 0, sizeof(*out));

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT t.id, t.name, t.description, t.icon, t.color, "
                         "t.category, COALESCE(c.name, ''), t.created_at, "
                         "t.updated_at FROM ci_types t "
                         "LEFT JOIN ci_categories c ON c.id = t.category "
                         "WHERE t.id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read CI type");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (read_type_row(stmt, out) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "CI type value is too long");
      return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "CI type not found");
    return -1;
  }
  cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read CI type");
  return -1;
}

int cmdb_types_find_by_name(cmdb_db_t *db, const char *name,
                           cmdb_ci_type_t *out, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || name == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid type lookup");
    return -1;
  }
  memset(out, 0, sizeof(*out));

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT t.id, t.name, t.description, t.icon, t.color, "
                         "t.category, COALESCE(c.name, ''), t.created_at, "
                         "t.updated_at FROM ci_types t "
                         "LEFT JOIN ci_categories c ON c.id = t.category "
                         "WHERE t.name = ? COLLATE NOCASE;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read CI type");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (read_type_row(stmt, out) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "CI type value is too long");
      return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "CI type not found");
    return -1;
  }
  cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read CI type");
  return -1;
}

static int audit_type(cmdb_db_t *db, const char *action, const char *id,
                      const char *name, cmdb_error_t *err) {
  cJSON *detail = cJSON_CreateObject();
  char *json;
  int rc;

  if (detail == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create audit detail");
    return -1;
  }
  cJSON_AddStringToObject(detail, "name", name != NULL ? name : "");
  json = cJSON_PrintUnformatted(detail);
  cJSON_Delete(detail);
  if (json == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create audit detail");
    return -1;
  }
  rc = cmdb_audit_add(db, cmdb_audit_actor(), action, "ci_type", id, json, err);
  free(json);
  return rc;
}

int cmdb_types_create(cmdb_db_t *db, const char *name, const char *description,
                      const char *icon, const char *color,
                      const char *category_id, char out_id[37],
                      cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char id[37];
  char now[21];
  int rc;

  if (db == NULL || name == NULL || out_id == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI type arguments");
    return -1;
  }
  out_id[0] = '\0';
  if (validate_text(name, CMDB_MAX_TYPE_NAME - 1, 1, "CI type name", err) != 0 ||
      validate_text(description, CMDB_MAX_DESCRIPTION - 1, 0,
                    "CI type description", err) != 0 ||
      validate_text(icon, 63, 0, "CI type icon", err) != 0 ||
      validate_text(color, 15, 0, "CI type color", err) != 0 ||
      validate_text(category_id, 36, 0, "CI type category", err) != 0) {
    return -1;
  }
  if (strlen(name) == 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "CI type name is required");
    return -1;
  }
  if (category_id != NULL && category_id[0] != '\0' &&
      !cmdb_categories_exists(db, category_id)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Category not found");
    return -1;
  }
  if (cmdb_random_uuid(id) != 0) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot generate CI type ID");
    return -1;
  }
  cmdb_iso8601_now(now);

  if (cmdb_db_savepoint_begin(db, "type_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "INSERT INTO ci_types"
                         "(id, name, description, icon, color, category, "
                         "created_at, updated_at) "
                         "VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare CI type insert");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, description != NULL ? description : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, icon != NULL ? icon : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, color != NULL ? color : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, category_id != NULL ? category_id : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, now, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_CONSTRAINT_UNIQUE || rc == SQLITE_CONSTRAINT) {
      cmdb_error_set(err, CMDB_ERR_CONFLICT,
                     "A CI type with that name already exists");
    } else {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot create CI type");
      cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
    }
    goto fail;
  }
  if (audit_type(db, "create", id, name, err) != 0) {
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "type_write", err) != 0) {
    return -1;
  }
  (void)cmdb_str_copy(out_id, 37, id);
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "type_write", NULL);
  return -1;
}

int cmdb_types_update(cmdb_db_t *db, const char *id, const char *name,
                      const char *description, const char *icon,
                      const char *color, const char *category_id,
                      cmdb_error_t *err) {
  cmdb_ci_type_t existing;
  sqlite3_stmt *stmt = NULL;
  char now[21];
  int rc;

  if (db == NULL || id == NULL || name == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI type arguments");
    return -1;
  }
  if (cmdb_types_get(db, id, &existing, err) != 0) {
    return -1;
  }
  if (validate_text(name, CMDB_MAX_TYPE_NAME - 1, 1, "CI type name", err) != 0 ||
      validate_text(description, CMDB_MAX_DESCRIPTION - 1, 0,
                    "CI type description", err) != 0 ||
      validate_text(icon, 63, 0, "CI type icon", err) != 0 ||
      validate_text(color, 15, 0, "CI type color", err) != 0 ||
      validate_text(category_id, 36, 0, "CI type category", err) != 0) {
    return -1;
  }
  if (strlen(name) == 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "CI type name is required");
    return -1;
  }
  if (category_id != NULL && category_id[0] != '\0' &&
      !cmdb_categories_exists(db, category_id)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Category not found");
    return -1;
  }

  cmdb_iso8601_now(now);
  if (cmdb_db_savepoint_begin(db, "type_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "UPDATE ci_types SET name = ?, description = ?, "
                         "icon = ?, color = ?, category = ?, updated_at = ? "
                         "WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare CI type update");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, description != NULL ? description : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, icon != NULL ? icon : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, color != NULL ? color : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, category_id != NULL ? category_id : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_CONSTRAINT_UNIQUE || rc == SQLITE_CONSTRAINT) {
      cmdb_error_set(err, CMDB_ERR_CONFLICT,
                     "A CI type with that name already exists");
    } else {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot update CI type");
    }
    goto fail;
  }
  if (audit_type(db, "update", id, name, err) != 0) {
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "type_write", err) != 0) {
    return -1;
  }
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "type_write", NULL);
  return -1;
}

int cmdb_types_delete(cmdb_db_t *db, const char *id, cmdb_error_t *err) {
  cmdb_ci_type_t existing;
  sqlite3_stmt *stmt = NULL;
  int count = 0;
  int rc;

  if (db == NULL || id == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI type arguments");
    return -1;
  }
  if (cmdb_types_get(db, id, &existing, err) != 0) {
    return -1;
  }

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT count(*) FROM cis WHERE type_id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot check CI type usage");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (count > 0) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "Cannot delete a CI type that is in use");
    return -1;
  }

  if (cmdb_db_savepoint_begin(db, "type_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "DELETE FROM ci_types WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare CI type delete");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot delete CI type");
    goto fail;
  }
  if (audit_type(db, "delete", id, existing.name, err) != 0) {
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "type_write", err) != 0) {
    return -1;
  }
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "type_write", NULL);
  return -1;
}

static int read_field_row(sqlite3_stmt *stmt, cmdb_type_field_t *out) {
  const unsigned char *text;

  text = sqlite3_column_text(stmt, 0);
  if (cmdb_str_copy(out->id, sizeof(out->id), (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 1);
  if (cmdb_str_copy(out->type_id, sizeof(out->type_id),
                    (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 2);
  if (cmdb_str_copy(out->key, sizeof(out->key), (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 3);
  if (cmdb_str_copy(out->label, sizeof(out->label),
                    (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 4);
  if (cmdb_str_copy(out->field_type, sizeof(out->field_type),
                    (const char *)text) != 0) {
    return -1;
  }
  out->required = sqlite3_column_int(stmt, 5);
  text = sqlite3_column_text(stmt, 6);
  if (cmdb_str_copy(out->options_json, sizeof(out->options_json),
                    (const char *)text) != 0) {
    return -1;
  }
  out->sort_order = sqlite3_column_int(stmt, 7);
  return 0;
}

int cmdb_type_fields_list(cmdb_db_t *db, const char *type_id,
                          cmdb_type_field_t **out, size_t *count,
                          cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  cmdb_type_field_t *fields;
  size_t total = 0;
  size_t i = 0;

  if (db == NULL || type_id == NULL || out == NULL || count == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid field list arguments");
    return -1;
  }
  *out = NULL;
  *count = 0;

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT count(*) FROM ci_type_fields WHERE type_id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot count CI type fields");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, type_id, -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    total = (size_t)sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (total == 0) {
    return 0;
  }

  fields = cmdb_calloc(total, sizeof(*fields));
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT id, type_id, field_key, label, field_type, "
                         "required, options_json, sort_order FROM ci_type_fields "
                         "WHERE type_id = ? ORDER BY sort_order, label COLLATE NOCASE;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    free(fields);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list CI type fields");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, type_id, -1, SQLITE_TRANSIENT);
  while (sqlite3_step(stmt) == SQLITE_ROW && i < total) {
    if (read_field_row(stmt, &fields[i]) != 0) {
      sqlite3_finalize(stmt);
      free(fields);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "CI type field value is too long");
      return -1;
    }
    i++;
  }
  sqlite3_finalize(stmt);
  *out = fields;
  *count = i;
  return 0;
}

void cmdb_type_fields_free(cmdb_type_field_t *fields) {
  free(fields);
}

int cmdb_type_field_get(cmdb_db_t *db, const char *id,
                        cmdb_type_field_t *out, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || id == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid field lookup");
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT id, type_id, field_key, label, field_type, "
                         "required, options_json, sort_order FROM ci_type_fields "
                         "WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read CI type field");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (read_field_row(stmt, out) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "CI type field value is too long");
      return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "CI type field not found");
    return -1;
  }
  cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read CI type field");
  return -1;
}

static int normalize_options(const char *options_json, char *out,
                             size_t out_size, cmdb_error_t *err) {
  cJSON *json;
  char *printed;
  int rc = -1;

  if (options_json == NULL || options_json[0] == '\0') {
    return cmdb_str_copy(out, out_size, "[]");
  }
  json = cJSON_Parse(options_json);
  if (json == NULL || !cJSON_IsArray(json)) {
    cJSON_Delete(json);
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Field options must be a JSON array");
    return -1;
  }
  printed = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);
  if (printed == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot encode field options");
    return -1;
  }
  if (cmdb_str_copy(out, out_size, printed) == 0) {
    rc = 0;
  } else {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Field options are too long");
  }
  free(printed);
  return rc;
}

static int validate_field_input(cmdb_db_t *db, const char *type_id,
                                const cmdb_type_field_input_t *input,
                                char *options_out, size_t options_size,
                                cmdb_error_t *err) {
  static const char *allowed_types[] = {
    "text", "number", "date", "boolean", "url", "select", NULL
  };
  size_t i;
  int allowed = 0;

  if (db == NULL || type_id == NULL || input == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid field arguments");
    return -1;
  }
  if (!cmdb_field_key_valid(input->key)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Field key must match [a-z][a-z0-9_]{0,63}");
    return -1;
  }
  if (validate_text(input->label, CMDB_MAX_FIELD_LABEL - 1, 1,
                    "Field label", err) != 0 ||
      validate_text(input->field_type, CMDB_MAX_FIELD_TYPE - 1, 1,
                    "Field type", err) != 0) {
    return -1;
  }
  for (i = 0; allowed_types[i] != NULL; i++) {
    if (strcmp(input->field_type, allowed_types[i]) == 0) {
      allowed = 1;
      break;
    }
  }
  if (!allowed) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Unsupported field type");
    return -1;
  }
  if (input->sort_order < 0 || input->sort_order > 10000) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Field sort order is out of range");
    return -1;
  }
  if (normalize_options(input->options_json, options_out, options_size, err) != 0) {
    return -1;
  }
  {
    cmdb_ci_type_t type;
    if (cmdb_types_get(db, type_id, &type, err) != 0) {
      return -1;
    }
  }
  return 0;
}

int cmdb_type_field_add(cmdb_db_t *db, const char *type_id,
                        const cmdb_type_field_input_t *input,
                        char out_id[37], cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char options[1024];
  char id[37];
  int rc;

  if (out_id == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid field output");
    return -1;
  }
  out_id[0] = '\0';
  if (validate_field_input(db, type_id, input, options, sizeof(options), err) != 0) {
    return -1;
  }
  if (cmdb_random_uuid(id) != 0) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot generate field ID");
    return -1;
  }
  if (cmdb_db_savepoint_begin(db, "field_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "INSERT INTO ci_type_fields"
                         "(id, type_id, field_key, label, field_type, required, "
                         "options_json, sort_order) VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare field insert");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, type_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, input->key, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, input->label, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, input->field_type, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 6, input->required ? 1 : 0);
  sqlite3_bind_text(stmt, 7, options, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 8, input->sort_order);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_CONSTRAINT_UNIQUE || rc == SQLITE_CONSTRAINT) {
      cmdb_error_set(err, CMDB_ERR_CONFLICT,
                     "A field with that key already exists on the CI type");
    } else {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot create field");
    }
    goto fail;
  }
  if (cmdb_audit_add(db, cmdb_audit_actor(), "create", "ci_type_field", id,
                     input->key, err) != 0) {
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "field_write", err) != 0) {
    return -1;
  }
  (void)cmdb_str_copy(out_id, 37, id);
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "field_write", NULL);
  return -1;
}

int cmdb_type_field_update(cmdb_db_t *db, const char *id,
                           const cmdb_type_field_input_t *input,
                           cmdb_error_t *err) {
  cmdb_type_field_t existing;
  sqlite3_stmt *stmt = NULL;
  char options[1024];
  int rc;

  if (db == NULL || id == NULL || input == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid field arguments");
    return -1;
  }
  if (cmdb_type_field_get(db, id, &existing, err) != 0) {
    return -1;
  }
  if (validate_field_input(db, existing.type_id, input, options, sizeof(options), err) != 0) {
    return -1;
  }
  if (cmdb_db_savepoint_begin(db, "field_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "UPDATE ci_type_fields SET field_key = ?, label = ?, "
                         "field_type = ?, required = ?, options_json = ?, "
                         "sort_order = ? WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare field update");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, input->key, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, input->label, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, input->field_type, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 4, input->required ? 1 : 0);
  sqlite3_bind_text(stmt, 5, options, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 6, input->sort_order);
  sqlite3_bind_text(stmt, 7, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_CONSTRAINT_UNIQUE || rc == SQLITE_CONSTRAINT) {
      cmdb_error_set(err, CMDB_ERR_CONFLICT,
                     "A field with that key already exists on the CI type");
    } else {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot update field");
    }
    goto fail;
  }
  if (cmdb_audit_add(db, cmdb_audit_actor(), "update", "ci_type_field", id,
                     input->key, err) != 0) {
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "field_write", err) != 0) {
    return -1;
  }
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "field_write", NULL);
  return -1;
}

int cmdb_type_field_delete(cmdb_db_t *db, const char *id,
                           cmdb_error_t *err) {
  cmdb_type_field_t existing;
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || id == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid field arguments");
    return -1;
  }
  if (cmdb_type_field_get(db, id, &existing, err) != 0) {
    return -1;
  }
  if (cmdb_db_savepoint_begin(db, "field_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "DELETE FROM ci_type_fields WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare field delete");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot delete field");
    goto fail;
  }
  if (cmdb_audit_add(db, cmdb_audit_actor(), "delete", "ci_type_field", id,
                     existing.key, err) != 0) {
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "field_write", err) != 0) {
    return -1;
  }
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "field_write", NULL);
  return -1;
}
