#include "cmdb/diagram.h"

#include "cmdb/audit.h"
#include "cmdb/util.h"

#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

static int diagram_validate(const char *name, const char *xml,
                            cmdb_error_t *err) {
  size_t name_len;
  size_t xml_len;

  if (name == NULL || name[0] == '\0') {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Diagram name is required");
    return -1;
  }
  name_len = strlen(name);
  if (name_len >= CMDB_MAX_DIAGRAM_NAME) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Diagram name is too long");
    return -1;
  }
  if (!cmdb_utf8_valid(name, name_len) || cmdb_has_control_chars(name)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Diagram name is invalid");
    return -1;
  }
  if (xml == NULL || xml[0] == '\0') {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Diagram content is required");
    return -1;
  }
  xml_len = strlen(xml);
  if (xml_len > CMDB_MAX_DIAGRAM_XML) {
    cmdb_error_set(err, CMDB_ERR_LIMIT, "Diagram content is too large");
    return -1;
  }
  if (!cmdb_utf8_valid(xml, xml_len)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Diagram content is not UTF-8");
    return -1;
  }
  if (strstr(xml, "<mxfile") == NULL) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Diagram content must be draw.io XML");
    return -1;
  }
  return 0;
}

static int diagram_fill(sqlite3_stmt *stmt, cmdb_diagram_t *out,
                        int include_xml) {
  const char *text;

  memset(out, 0, sizeof(*out));
  text = (const char *)sqlite3_column_text(stmt, 0);
  if (cmdb_str_copy(out->id, sizeof(out->id), text) != 0) {
    return -1;
  }
  text = (const char *)sqlite3_column_text(stmt, 1);
  if (cmdb_str_copy(out->name, sizeof(out->name), text) != 0) {
    return -1;
  }
  if (include_xml) {
    text = (const char *)sqlite3_column_text(stmt, 2);
    out->drawio_xml = cmdb_strdup(text != NULL ? text : "");
  }
  text = (const char *)sqlite3_column_text(stmt, include_xml ? 3 : 2);
  if (cmdb_str_copy(out->created_at, sizeof(out->created_at), text) != 0) {
    return -1;
  }
  text = (const char *)sqlite3_column_text(stmt, include_xml ? 4 : 3);
  if (cmdb_str_copy(out->updated_at, sizeof(out->updated_at), text) != 0) {
    return -1;
  }
  return 0;
}

int cmdb_diagram_list(cmdb_db_t *db, cmdb_diagram_t **out, size_t *count,
                      int include_xml, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  sqlite3_stmt *counter = NULL;
  cmdb_diagram_t *items;
  size_t total = 0;
  size_t index = 0;
  int rc;

  if (db == NULL || out == NULL || count == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid diagram list");
    return -1;
  }
  *out = NULL;
  *count = 0;
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT count(*) FROM diagrams;", -1, &counter,
                         NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot count diagrams");
    return -1;
  }
  if (sqlite3_step(counter) == SQLITE_ROW) {
    total = (size_t)sqlite3_column_int64(counter, 0);
  }
  sqlite3_finalize(counter);
  if (total == 0) {
    return 0;
  }
  items = cmdb_calloc(total, sizeof(*items));
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        include_xml
          ? "SELECT id, name, drawio_xml, created_at, updated_at "
            "FROM diagrams ORDER BY updated_at DESC;"
          : "SELECT id, name, created_at, updated_at "
            "FROM diagrams ORDER BY updated_at DESC;",
        -1, &stmt, NULL) != SQLITE_OK) {
    free(items);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list diagrams");
    return -1;
  }
  while (index < total && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (diagram_fill(stmt, &items[index], include_xml) != 0) {
      sqlite3_finalize(stmt);
      cmdb_diagram_free_list(items, index);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Diagram value is too long");
      return -1;
    }
    index++;
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
    cmdb_diagram_free_list(items, index);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list diagrams");
    return -1;
  }
  *out = items;
  *count = index;
  return 0;
}

int cmdb_diagram_get(cmdb_db_t *db, const char *id, cmdb_diagram_t *out,
                     cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || id == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid diagram lookup");
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT id, name, drawio_xml, created_at, updated_at "
        "FROM diagrams WHERE id = ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read diagram");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (diagram_fill(stmt, out, 1) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Diagram value is too long");
      return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  cmdb_error_set(err, rc == SQLITE_DONE ? CMDB_ERR_NOT_FOUND
                                        : CMDB_ERR_DATABASE,
                 rc == SQLITE_DONE ? "Diagram not found"
                                   : "Cannot read diagram");
  return -1;
}

int cmdb_diagram_save(cmdb_db_t *db, const char *id, const char *name,
                      const char *xml, char out_id[37], cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char uuid[37];
  char now[21];
  int create;
  int rc;

  if (db == NULL || out_id == NULL || diagram_validate(name, xml, err) != 0) {
    return -1;
  }
  out_id[0] = '\0';
  create = id == NULL || id[0] == '\0';
  if (create) {
    if (cmdb_random_uuid(uuid) != 0) {
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot generate diagram ID");
      return -1;
    }
  } else if (!cmdb_uuid_valid(id)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Invalid diagram ID");
    return -1;
  } else {
    (void)cmdb_str_copy(uuid, sizeof(uuid), id);
  }
  cmdb_iso8601_now(now);
  if (cmdb_db_savepoint_begin(db, "diagram_write", err) != 0) {
    return -1;
  }
  if (create) {
    if (sqlite3_prepare_v2(
          cmdb_db_sqlite(db),
          "INSERT INTO diagrams(id, name, drawio_xml, created_at, updated_at) "
          "VALUES(?, ?, ?, ?, ?);",
          -1, &stmt, NULL) != SQLITE_OK) {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare diagram insert");
      goto fail;
    }
    sqlite3_bind_text(stmt, 1, uuid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, xml, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, now, -1, SQLITE_TRANSIENT);
  } else {
    if (sqlite3_prepare_v2(
          cmdb_db_sqlite(db),
          "UPDATE diagrams SET name = ?, drawio_xml = ?, updated_at = ? "
          "WHERE id = ?;",
          -1, &stmt, NULL) != SQLITE_OK) {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare diagram update");
      goto fail;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, xml, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, uuid, -1, SQLITE_TRANSIENT);
  }
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot save diagram");
    goto fail;
  }
  if (!create && cmdb_db_changes(db) == 0) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "Diagram not found");
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "diagram_write", err) != 0) {
    return -1;
  }
  {
    cJSON *detail = cJSON_CreateObject();
    char *json = NULL;
    if (detail != NULL) {
      cJSON_AddStringToObject(detail, "name", name);
      cJSON_AddNumberToObject(detail, "bytes", (double)strlen(xml));
      json = cJSON_PrintUnformatted(detail);
      cJSON_Delete(detail);
    }
    if (json != NULL) {
      (void)cmdb_audit_add(db, cmdb_audit_actor(), create ? "create" : "update",
                           "diagram", uuid, json, NULL);
      free(json);
    }
  }
  (void)cmdb_str_copy(out_id, 37, uuid);
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "diagram_write", NULL);
  return -1;
}

int cmdb_diagram_delete(cmdb_db_t *db, const char *id, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || id == NULL || !cmdb_uuid_valid(id)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid diagram ID");
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "DELETE FROM diagrams WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot delete diagram");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot delete diagram");
    return -1;
  }
  (void)cmdb_audit_add(db, cmdb_audit_actor(), "delete", "diagram", id, "{}", NULL);
  return 0;
}

void cmdb_diagram_free(cmdb_diagram_t *diagram) {
  if (diagram == NULL) {
    return;
  }
  free(diagram->drawio_xml);
  memset(diagram, 0, sizeof(*diagram));
}

void cmdb_diagram_free_list(cmdb_diagram_t *diagrams, size_t count) {
  size_t i;
  if (diagrams == NULL) {
    return;
  }
  for (i = 0; i < count; i++) {
    free(diagrams[i].drawio_xml);
  }
  free(diagrams);
}
