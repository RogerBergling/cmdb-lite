#include "cmdb/ci.h"

#include "cmdb/audit.h"
#include "cmdb/json_util.h"
#include "cmdb/types.h"
#include "cmdb/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int validate_text_field(const char *value, size_t max_len, int required,
                               int allow_newlines, const char *label,
                               cmdb_error_t *err) {
  size_t len;

  if (value == NULL || value[0] == '\0') {
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
  if (!allow_newlines && cmdb_has_control_chars(value)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "%s contains control characters", label);
    return -1;
  }
  if (allow_newlines) {
    const unsigned char *p = (const unsigned char *)value;
    while (*p != '\0') {
      if ((*p < 0x20 && *p != '\n' && *p != '\r' && *p != '\t') ||
          *p == 0x7F) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "%s contains control characters", label);
        return -1;
      }
      p++;
    }
  }
  return 0;
}

static const char *attr_value_for_key(const cmdb_attr_input_t *attrs,
                                      size_t count, const char *key) {
  size_t i;
  for (i = 0; i < count; i++) {
    if (attrs[i].key != NULL && strcmp(attrs[i].key, key) == 0) {
      return attrs[i].value != NULL ? attrs[i].value : "";
    }
  }
  return NULL;
}

static int validate_attrs(cmdb_db_t *db, const cmdb_ci_input_t *input,
                          cmdb_error_t *err) {
  size_t i;
  size_t j;
  cmdb_type_field_t *fields = NULL;
  size_t field_count = 0;

  if (input->attr_count > CMDB_MAX_ATTRIBUTES) {
    cmdb_error_set(err, CMDB_ERR_LIMIT,
                   "Too many custom attributes (maximum %u)",
                   (unsigned int)CMDB_MAX_ATTRIBUTES);
    return -1;
  }
  if (input->attr_count > 0 && input->attrs == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Missing attribute data");
    return -1;
  }

  for (i = 0; i < input->attr_count; i++) {
    if (!cmdb_field_key_valid(input->attrs[i].key)) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "Invalid custom field key");
      return -1;
    }
    if (validate_text_field(input->attrs[i].value, CMDB_MAX_VALUE - 1, 0, 1,
                            "Custom field value", err) != 0) {
      return -1;
    }
    for (j = 0; j < i; j++) {
      if (strcmp(input->attrs[j].key, input->attrs[i].key) == 0) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Duplicate custom field '%s'", input->attrs[i].key);
        return -1;
      }
    }
  }

  if (cmdb_type_fields_list(db, input->type_id, &fields, &field_count, err) != 0) {
    return -1;
  }
  for (i = 0; i < field_count; i++) {
    if (fields[i].required) {
      const char *value = attr_value_for_key(input->attrs, input->attr_count,
                                             fields[i].key);
      if (value == NULL || value[0] == '\0') {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Required custom field '%s' is missing", fields[i].key);
        cmdb_type_fields_free(fields);
        return -1;
      }
    }
  }
  cmdb_type_fields_free(fields);
  return 0;
}

int cmdb_ci_validate_fields(const cmdb_ci_input_t *input, cmdb_error_t *err) {
  if (input == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI arguments");
    return -1;
  }
  if (input->id != NULL && input->id[0] != '\0' && !cmdb_uuid_valid(input->id)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "CI ID must be a valid UUID");
    return -1;
  }
  if (validate_text_field(input->name, CMDB_MAX_NAME - 1, 1, 0,
                          "CI name", err) != 0 ||
      validate_text_field(input->type_id, 36, 1, 0,
                          "CI type", err) != 0 ||
      validate_text_field(input->description, CMDB_MAX_DESCRIPTION - 1, 0, 1,
                          "Description", err) != 0 ||
      validate_text_field(input->environment, 63, 0, 0,
                          "Environment", err) != 0 ||
      validate_text_field(input->status, 63, 0, 0, "Status", err) != 0 ||
      validate_text_field(input->business_service, CMDB_MAX_NAME - 1, 0, 0,
                          "Business service", err) != 0 ||
      validate_text_field(input->technical_owner, CMDB_MAX_NAME - 1, 0, 0,
                          "Technical owner", err) != 0 ||
      validate_text_field(input->support_team, CMDB_MAX_NAME - 1, 0, 0,
                          "Support team", err) != 0 ||
      validate_text_field(input->criticality, 31, 0, 0,
                          "Criticality", err) != 0 ||
      validate_text_field(input->hosting_location, CMDB_MAX_NAME - 1, 0, 0,
                          "Hosting/location", err) != 0 ||
      validate_text_field(input->external_id, CMDB_MAX_EXTERNAL_ID - 1, 0, 0,
                          "External ID", err) != 0) {
    return -1;
  }
  return 0;
}

static int validate_ci_input(cmdb_db_t *db, const cmdb_ci_input_t *input,
                             cmdb_error_t *err) {
  cmdb_ci_type_t type;

  if (db == NULL || input == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI arguments");
    return -1;
  }
  if (cmdb_ci_validate_fields(input, err) != 0) {
    return -1;
  }
  if (cmdb_types_get(db, input->type_id, &type, err) != 0) {
    return -1;
  }
  (void)type;
  if (validate_attrs(db, input, err) != 0) {
    return -1;
  }
  return 0;
}

static int insert_attributes(cmdb_db_t *db, const char *ci_id,
                             const cmdb_attr_input_t *attrs, size_t count,
                             cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  size_t i;
  int rc;

  if (count == 0) {
    return 0;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "INSERT INTO ci_attributes(ci_id, field_key, value) "
                         "VALUES(?, ?, ?);",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare attribute write");
    return -1;
  }
  for (i = 0; i < count; i++) {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_bind_text(stmt, 1, ci_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, attrs[i].key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, attrs[i].value != NULL ? attrs[i].value : "",
                      -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot write CI attributes");
      cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
      return -1;
    }
  }
  sqlite3_finalize(stmt);
  return 0;
}

static int audit_ci(cmdb_db_t *db, const char *action, const char *id,
                    const cmdb_ci_input_t *input, cmdb_error_t *err) {
  cJSON *detail = cJSON_CreateObject();
  char *json;
  int rc;

  if (detail == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create audit detail");
    return -1;
  }
  cJSON_AddStringToObject(detail, "name",
                          input->name != NULL ? input->name : "");
  cJSON_AddStringToObject(detail, "type_id",
                          input->type_id != NULL ? input->type_id : "");
  json = cJSON_PrintUnformatted(detail);
  cJSON_Delete(detail);
  if (json == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create audit detail");
    return -1;
  }
  rc = cmdb_audit_add(db, cmdb_audit_actor(), action, "ci", id, json, err);
  free(json);
  return rc;
}

int cmdb_ci_validate(cmdb_db_t *db, const cmdb_ci_input_t *input,
                     cmdb_error_t *err) {
  return validate_ci_input(db, input, err);
}

int cmdb_ci_exists(cmdb_db_t *db, const char *id, int *exists,
                   cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || id == NULL || exists == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI lookup");
    return -1;
  }
  *exists = 0;
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT 1 FROM cis WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot check CI existence");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc == SQLITE_ROW) {
    *exists = 1;
    return 0;
  }
  if (rc == SQLITE_DONE) {
    return 0;
  }
  cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot check CI existence");
  return -1;
}

int cmdb_ci_create_ex(cmdb_db_t *db, const cmdb_ci_input_t *input,
                      int write_audit, char out_id[37], cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char id[37];
  char now[21];
  int rc;

  if (out_id == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI output");
    return -1;
  }
  out_id[0] = '\0';
  if (validate_ci_input(db, input, err) != 0) {
    return -1;
  }
  if (input->id != NULL && input->id[0] != '\0') {
    (void)cmdb_str_copy(id, sizeof(id), input->id);
  } else {
    if (cmdb_random_uuid(id) != 0) {
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot generate CI ID");
      return -1;
    }
  }
  cmdb_iso8601_now(now);

  if (cmdb_db_savepoint_begin(db, "ci_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "INSERT INTO cis"
                         "(id, name, type_id, description, environment, status, "
                         "business_service, technical_owner, support_team, "
                         "criticality, hosting_location, external_id, "
                         "created_at, updated_at) "
                         "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare CI insert");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, input->name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, input->type_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, input->description != NULL ? input->description : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, input->environment != NULL ? input->environment : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, input->status != NULL ? input->status : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7,
                    input->business_service != NULL ? input->business_service : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8,
                    input->technical_owner != NULL ? input->technical_owner : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 9,
                    input->support_team != NULL ? input->support_team : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 10,
                    input->criticality != NULL ? input->criticality : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 11,
                    input->hosting_location != NULL ? input->hosting_location : "",
                    -1, SQLITE_TRANSIENT);
  if (input->external_id == NULL || input->external_id[0] == '\0') {
    sqlite3_bind_null(stmt, 12);
  } else {
    sqlite3_bind_text(stmt, 12, input->external_id, -1, SQLITE_TRANSIENT);
  }
  sqlite3_bind_text(stmt, 13, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 14, now, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_CONSTRAINT_UNIQUE || rc == SQLITE_CONSTRAINT) {
      cmdb_error_set(err, CMDB_ERR_CONFLICT,
                     "CI ID or external ID already exists");
    } else {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot create CI");
      cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
    }
    goto fail;
  }
  if (insert_attributes(db, id, input->attrs, input->attr_count, err) != 0) {
    goto fail;
  }
  if (write_audit && audit_ci(db, "create", id, input, err) != 0) {
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "ci_write", err) != 0) {
    return -1;
  }
  (void)cmdb_str_copy(out_id, 37, id);
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "ci_write", NULL);
  return -1;
}

int cmdb_ci_create(cmdb_db_t *db, const cmdb_ci_input_t *input,
                   char out_id[37], cmdb_error_t *err) {
  return cmdb_ci_create_ex(db, input, 1, out_id, err);
}

int cmdb_ci_update_ex(cmdb_db_t *db, const char *id,
                      const cmdb_ci_input_t *input, int write_audit,
                      cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char now[21];
  int rc;

  if (db == NULL || id == NULL || !cmdb_uuid_valid(id)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI ID");
    return -1;
  }
  if (validate_ci_input(db, input, err) != 0) {
    return -1;
  }

  cmdb_iso8601_now(now);
  if (cmdb_db_savepoint_begin(db, "ci_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "UPDATE cis SET name = ?, type_id = ?, description = ?, "
                         "environment = ?, status = ?, business_service = ?, "
                         "technical_owner = ?, support_team = ?, criticality = ?, "
                         "hosting_location = ?, external_id = ?, updated_at = ? "
                         "WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare CI update");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, input->name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, input->type_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, input->description != NULL ? input->description : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, input->environment != NULL ? input->environment : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, input->status != NULL ? input->status : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6,
                    input->business_service != NULL ? input->business_service : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7,
                    input->technical_owner != NULL ? input->technical_owner : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8,
                    input->support_team != NULL ? input->support_team : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 9,
                    input->criticality != NULL ? input->criticality : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 10,
                    input->hosting_location != NULL ? input->hosting_location : "",
                    -1, SQLITE_TRANSIENT);
  if (input->external_id == NULL || input->external_id[0] == '\0') {
    sqlite3_bind_null(stmt, 11);
  } else {
    sqlite3_bind_text(stmt, 11, input->external_id, -1, SQLITE_TRANSIENT);
  }
  sqlite3_bind_text(stmt, 12, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 13, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_CONSTRAINT_UNIQUE || rc == SQLITE_CONSTRAINT) {
      cmdb_error_set(err, CMDB_ERR_CONFLICT,
                     "External ID already exists");
    } else {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot update CI");
      cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
    }
    goto fail;
  }
  if (sqlite3_changes(cmdb_db_sqlite(db)) == 0) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "CI not found");
    goto fail;
  }

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "DELETE FROM ci_attributes WHERE ci_id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare attribute delete");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot replace CI attributes");
    goto fail;
  }
  if (insert_attributes(db, id, input->attrs, input->attr_count, err) != 0) {
    goto fail;
  }
  if (write_audit && audit_ci(db, "update", id, input, err) != 0) {
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "ci_write", err) != 0) {
    return -1;
  }
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "ci_write", NULL);
  return -1;
}

int cmdb_ci_update(cmdb_db_t *db, const char *id, const cmdb_ci_input_t *input,
                   cmdb_error_t *err) {
  return cmdb_ci_update_ex(db, id, input, 1, err);
}

int cmdb_ci_delete(cmdb_db_t *db, const char *id, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || id == NULL || !cmdb_uuid_valid(id)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI ID");
    return -1;
  }

  if (cmdb_db_savepoint_begin(db, "ci_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "DELETE FROM cis WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare CI delete");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot delete CI");
    goto fail;
  }
  if (sqlite3_changes(cmdb_db_sqlite(db)) == 0) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "CI not found");
    goto fail;
  }
  if (cmdb_audit_add(db, cmdb_audit_actor(), "delete", "ci", id, "", err) != 0) {
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "ci_write", err) != 0) {
    return -1;
  }
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "ci_write", NULL);
  return -1;
}

static int read_ci_row(sqlite3_stmt *stmt, cmdb_ci_t *out) {
  const unsigned char *text;
  int column = 0;

#define READ_TEXT(field) \
  do { \
    text = sqlite3_column_text(stmt, column++); \
    if (cmdb_str_copy(out->field, sizeof(out->field), \
                      (const char *)text) != 0) { \
      return -1; \
    } \
  } while (0)

  READ_TEXT(id);
  READ_TEXT(name);
  READ_TEXT(type_id);
  READ_TEXT(type_name);
  READ_TEXT(description);
  READ_TEXT(environment);
  READ_TEXT(status);
  READ_TEXT(business_service);
  READ_TEXT(technical_owner);
  READ_TEXT(support_team);
  READ_TEXT(criticality);
  READ_TEXT(hosting_location);
  READ_TEXT(external_id);
  READ_TEXT(created_at);
  READ_TEXT(updated_at);

#undef READ_TEXT

  return 0;
}

static int load_attributes(cmdb_db_t *db, cmdb_ci_t *ci, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  size_t total = 0;
  size_t i = 0;

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT count(*) FROM ci_attributes WHERE ci_id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot count CI attributes");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, ci->id, -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    total = (size_t)sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  stmt = NULL;

  if (total == 0) {
    return 0;
  }
  ci->attrs = cmdb_calloc(total, sizeof(*ci->attrs));
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT field_key, value FROM ci_attributes "
                         "WHERE ci_id = ? ORDER BY field_key;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    free(ci->attrs);
    ci->attrs = NULL;
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list CI attributes");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, ci->id, -1, SQLITE_TRANSIENT);
  while (sqlite3_step(stmt) == SQLITE_ROW && i < total) {
    const unsigned char *key = sqlite3_column_text(stmt, 0);
    const unsigned char *value = sqlite3_column_text(stmt, 1);
    if (cmdb_str_copy(ci->attrs[i].key, sizeof(ci->attrs[i].key),
                      (const char *)key) != 0 ||
        cmdb_str_copy(ci->attrs[i].value, sizeof(ci->attrs[i].value),
                      (const char *)(value != NULL ? value : (const unsigned char *)"")) != 0) {
      sqlite3_finalize(stmt);
      free(ci->attrs);
      ci->attrs = NULL;
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "CI attribute is too long");
      return -1;
    }
    i++;
  }
  sqlite3_finalize(stmt);
  ci->attr_count = i;
  return 0;
}

int cmdb_ci_get(cmdb_db_t *db, const char *id, cmdb_ci_t *out,
                cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || id == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI lookup");
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT c.id, c.name, c.type_id, t.name, c.description, "
                         "c.environment, c.status, c.business_service, "
                         "c.technical_owner, c.support_team, c.criticality, "
                         "c.hosting_location, c.external_id, c.created_at, "
                         "c.updated_at FROM cis c "
                         "JOIN ci_types t ON t.id = c.type_id WHERE c.id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read CI");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (read_ci_row(stmt, out) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "CI value is too long");
      return -1;
    }
    sqlite3_finalize(stmt);
    if (load_attributes(db, out, err) != 0) {
      cmdb_ci_free(out);
      return -1;
    }
    return 0;
  }
  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "CI not found");
    return -1;
  }
  cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read CI");
  return -1;
}

static const char where_clause[] =
  " WHERE (?1 = '' OR instr(lower(c.name), lower(?1)) > 0 "
  "OR instr(lower(c.description), lower(?1)) > 0 "
  "OR instr(lower(c.business_service), lower(?1)) > 0 "
  "OR instr(lower(c.technical_owner), lower(?1)) > 0) "
  "AND (?2 = '' OR c.type_id = ?2) ";

int cmdb_ci_list(cmdb_db_t *db, const cmdb_ci_filter_t *filter,
                 cmdb_ci_t **out, size_t *count, size_t *total,
                 cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  cmdb_ci_t *items;
  const char *query;
  const char *type_id;
  size_t limit;
  size_t offset;
  size_t i = 0;
  size_t total_value = 0;
  char sql[1024];
  int rc;

  if (db == NULL || out == NULL || count == NULL || total == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI list arguments");
    return -1;
  }
  *out = NULL;
  *count = 0;
  *total = 0;
  query = filter != NULL && filter->query != NULL ? filter->query : "";
  type_id = filter != NULL && filter->type_id != NULL ? filter->type_id : "";
  limit = filter != NULL ? filter->limit : 100;
  offset = filter != NULL ? filter->offset : 0;
  if (limit == 0 || limit > CMDB_MAX_LIST_LIMIT) {
    limit = limit == 0 ? 100 : CMDB_MAX_LIST_LIMIT;
  }

  (void)snprintf(sql, sizeof(sql),
                 "SELECT count(*) FROM cis c%s;", where_clause);
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db), sql, -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot count CIs");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, query, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, type_id, -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    total_value = (size_t)sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  *total = total_value;

  if (total_value == 0) {
    return 0;
  }

  items = cmdb_calloc(total_value < limit ? total_value : limit, sizeof(*items));
  (void)snprintf(sql, sizeof(sql),
                 "SELECT c.id, c.name, c.type_id, t.name, c.description, "
                 "c.environment, c.status, c.business_service, "
                 "c.technical_owner, c.support_team, c.criticality, "
                 "c.hosting_location, c.external_id, c.created_at, "
                 "c.updated_at FROM cis c "
                 "JOIN ci_types t ON t.id = c.type_id%s"
                 "ORDER BY c.name COLLATE NOCASE LIMIT ?3 OFFSET ?4;",
                 where_clause);
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db), sql, -1, &stmt, NULL) != SQLITE_OK) {
    free(items);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list CIs");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, query, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, type_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 3, (sqlite3_int64)limit);
  sqlite3_bind_int64(stmt, 4, (sqlite3_int64)offset);
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW && i < limit) {
    if (read_ci_row(stmt, &items[i]) != 0) {
      sqlite3_finalize(stmt);
      free(items);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "CI value is too long");
      return -1;
    }
    i++;
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
    free(items);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list CIs");
    return -1;
  }
  *out = items;
  *count = i;
  return 0;
}

void cmdb_ci_free(cmdb_ci_t *ci) {
  if (ci == NULL) {
    return;
  }
  free(ci->attrs);
  ci->attrs = NULL;
  ci->attr_count = 0;
}

void cmdb_ci_free_list(cmdb_ci_t *cis, size_t count) {
  size_t i;
  if (cis == NULL) {
    return;
  }
  for (i = 0; i < count; i++) {
    free(cis[i].attrs);
  }
  free(cis);
}

int cmdb_ci_count(cmdb_db_t *db, size_t *out, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;

  if (db == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI count");
    return -1;
  }
  *out = 0;
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT count(*) FROM cis;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot count CIs");
    return -1;
  }
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    *out = (size_t)sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return 0;
}

int cmdb_ci_find_by_external_id(cmdb_db_t *db, const char *external_id,
                               char out_id[37], cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || external_id == NULL || external_id[0] == '\0' ||
      out_id == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid external ID lookup");
    return -1;
  }
  out_id[0] = '\0';
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT id FROM cis WHERE external_id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot look up external ID");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, external_id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    const unsigned char *id = sqlite3_column_text(stmt, 0);
    if (cmdb_str_copy(out_id, 37, (const char *)id) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "CI ID is too long");
      return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "CI not found");
    return -1;
  }
  cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot look up external ID");
  return -1;
}

int cmdb_ci_find_by_name_unique(cmdb_db_t *db, const char *name,
                               char out_id[37], cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;
  char found[37];
  int count = 0;

  if (db == NULL || name == NULL || name[0] == '\0' || out_id == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI name lookup");
    return -1;
  }
  out_id[0] = '\0';

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT id FROM cis WHERE name = ? COLLATE NOCASE;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot look up CI name");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (count == 0) {
      const unsigned char *id = sqlite3_column_text(stmt, 0);
      (void)cmdb_str_copy(found, sizeof(found), (const char *)id);
    }
    count++;
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot look up CI name");
    return -1;
  }
  if (count == 0) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "CI not found");
    return -1;
  }
  if (count > 1) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "CI name is ambiguous; use a CI ID or external ID");
    return -1;
  }
  (void)cmdb_str_copy(out_id, 37, found);
  return 0;
}

int cmdb_ci_resolve(cmdb_db_t *db, const char *token, char out_id[37],
                    cmdb_error_t *err) {
  if (db == NULL || token == NULL || token[0] == '\0' || out_id == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid CI reference");
    return -1;
  }

  if (cmdb_uuid_valid(token)) {
    cmdb_ci_t ci;
    if (cmdb_ci_get(db, token, &ci, err) == 0) {
      cmdb_ci_free(&ci);
      return cmdb_str_copy(out_id, 37, token);
    }
    if (err != NULL && err->code != CMDB_ERR_NOT_FOUND) {
      return -1;
    }
    cmdb_error_clear(err);
  }

  if (cmdb_ci_find_by_external_id(db, token, out_id, err) == 0) {
    return 0;
  }
  if (err != NULL && err->code != CMDB_ERR_NOT_FOUND) {
    return -1;
  }
  cmdb_error_clear(err);

  return cmdb_ci_find_by_name_unique(db, token, out_id, err);
}
