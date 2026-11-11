#include "cmdb/relationships.h"

#include "cmdb/audit.h"
#include "cmdb/ci.h"
#include "cmdb/json_util.h"
#include "cmdb/util.h"

#include <stdlib.h>
#include <string.h>

static int read_rel_type_row(sqlite3_stmt *stmt, cmdb_relationship_type_t *out) {
  const unsigned char *text;

  text = sqlite3_column_text(stmt, 0);
  if (cmdb_str_copy(out->id, sizeof(out->id), (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 1);
  if (cmdb_str_copy(out->name, sizeof(out->name), (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 2);
  if (cmdb_str_copy(out->inverse_name, sizeof(out->inverse_name),
                    (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 3);
  if (cmdb_str_copy(out->description, sizeof(out->description),
                    (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 4);
  if (cmdb_str_copy(out->color, sizeof(out->color), (const char *)text) != 0) {
    return -1;
  }
  text = sqlite3_column_text(stmt, 5);
  if (cmdb_str_copy(out->default_impact, sizeof(out->default_impact),
                    (const char *)text) != 0) {
    return -1;
  }
  return 0;
}

int cmdb_relationship_impact_valid(const char *impact) {
  return impact != NULL &&
         (strcmp(impact, "availability") == 0 ||
          strcmp(impact, "degraded") == 0 ||
          strcmp(impact, "management") == 0 ||
          strcmp(impact, "monitoring") == 0 ||
          strcmp(impact, "informational") == 0);
}

int cmdb_relationship_types_list(cmdb_db_t *db,
                                 cmdb_relationship_type_t **out,
                                 size_t *count, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  size_t total = 0;
  size_t i = 0;
  cmdb_relationship_type_t *types;

  if (db == NULL || out == NULL || count == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid relationship type arguments");
    return -1;
  }
  *out = NULL;
  *count = 0;

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT count(*) FROM relationship_types;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot count relationship types");
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
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT id, name, inverse_name, description, color, "
                         "default_impact "
                         "FROM relationship_types ORDER BY name COLLATE NOCASE;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    free(types);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list relationship types");
    return -1;
  }
  while (sqlite3_step(stmt) == SQLITE_ROW && i < total) {
    if (read_rel_type_row(stmt, &types[i]) != 0) {
      sqlite3_finalize(stmt);
      free(types);
      cmdb_error_set(err, CMDB_ERR_INTERNAL,
                     "Relationship type value is too long");
      return -1;
    }
    i++;
  }
  sqlite3_finalize(stmt);
  *out = types;
  *count = i;
  return 0;
}

void cmdb_relationship_types_free(cmdb_relationship_type_t *types) {
  free(types);
}

int cmdb_relationship_type_find(cmdb_db_t *db, const char *name,
                                cmdb_relationship_type_t *out,
                                cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || name == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid relationship type lookup");
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT id, name, inverse_name, description, color, "
                         "default_impact "
                         "FROM relationship_types WHERE name = ? COLLATE NOCASE;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read relationship type");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (read_rel_type_row(stmt, out) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL,
                     "Relationship type value is too long");
      return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "Relationship type not found");
    return -1;
  }
  cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read relationship type");
  return -1;
}

int cmdb_relationship_type_get(cmdb_db_t *db, const char *id,
                               cmdb_relationship_type_t *out,
                               cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || id == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid relationship type lookup");
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT id, name, inverse_name, description, color, "
                         "default_impact "
                         "FROM relationship_types WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read relationship type");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (read_rel_type_row(stmt, out) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL,
                     "Relationship type value is too long");
      return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "Relationship type not found");
    return -1;
  }
  cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read relationship type");
  return -1;
}

static int read_rel_row(sqlite3_stmt *stmt, cmdb_relationship_t *out) {
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
  READ_TEXT(source_id);
  READ_TEXT(source_name);
  READ_TEXT(target_id);
  READ_TEXT(target_name);
  READ_TEXT(type_id);
  READ_TEXT(type_name);
  READ_TEXT(description);
  READ_TEXT(impact);
  READ_TEXT(created_at);
  READ_TEXT(updated_at);

#undef READ_TEXT
  return 0;
}

static int audit_relationship(cmdb_db_t *db, const char *action, const char *id,
                              const char *source_id, const char *target_id,
                              const char *type_id, const char *impact,
                              cmdb_error_t *err) {
  cJSON *detail = cJSON_CreateObject();
  char *json;
  int rc;

  if (detail == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create audit detail");
    return -1;
  }
  cJSON_AddStringToObject(detail, "source_id", source_id);
  cJSON_AddStringToObject(detail, "target_id", target_id);
  cJSON_AddStringToObject(detail, "type_id", type_id);
  cJSON_AddStringToObject(detail, "impact", impact != NULL ? impact : "");
  json = cJSON_PrintUnformatted(detail);
  cJSON_Delete(detail);
  if (json == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create audit detail");
    return -1;
  }
  rc = cmdb_audit_add(db, cmdb_audit_actor(), action, "relationship", id, json, err);
  free(json);
  return rc;
}

int cmdb_relationship_create_ex(cmdb_db_t *db, const char *source_id,
                                const char *target_id, const char *type_id,
                                const char *description, const char *impact,
                                int write_audit, char out_id[37],
                                cmdb_error_t *err) {
  cmdb_ci_t source;
  cmdb_ci_t target;
  cmdb_relationship_type_t type;
  const char *effective_impact;
  sqlite3_stmt *stmt = NULL;
  char id[37];
  char now[21];
  int rc;

  if (out_id == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid relationship output");
    return -1;
  }
  out_id[0] = '\0';
  if (db == NULL || source_id == NULL || target_id == NULL || type_id == NULL ||
      !cmdb_uuid_valid(source_id) || !cmdb_uuid_valid(target_id) ||
      !cmdb_uuid_valid(type_id)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid relationship arguments");
    return -1;
  }
  if (strcmp(source_id, target_id) == 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "A CI cannot have a relationship to itself");
    return -1;
  }
  if (description != NULL && strlen(description) > 1023) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Relationship description is too long");
    return -1;
  }
  if (description != NULL &&
      (!cmdb_utf8_valid(description, strlen(description)) ||
       cmdb_has_control_chars(description))) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Relationship description is invalid");
    return -1;
  }

  if (cmdb_ci_get(db, source_id, &source, err) != 0) {
    return -1;
  }
  cmdb_ci_free(&source);
  if (cmdb_ci_get(db, target_id, &target, err) != 0) {
    return -1;
  }
  cmdb_ci_free(&target);
  if (cmdb_relationship_type_get(db, type_id, &type, err) != 0) {
    return -1;
  }
  effective_impact = impact != NULL && impact[0] != '\0'
                       ? impact
                       : (type.default_impact[0] != '\0'
                            ? type.default_impact
                            : "availability");
  if (!cmdb_relationship_impact_valid(effective_impact)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Relationship impact must be availability, degraded, "
                   "management, monitoring, or informational");
    return -1;
  }

  if (cmdb_random_uuid(id) != 0) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot generate relationship ID");
    return -1;
  }
  cmdb_iso8601_now(now);

  if (cmdb_db_savepoint_begin(db, "rel_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "INSERT INTO relationships"
                         "(id, source_id, target_id, type_id, description, "
                         "impact, created_at, updated_at) "
                         "VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare relationship insert");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, source_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, target_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, type_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, description != NULL ? description : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, effective_impact, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, now, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_CONSTRAINT_UNIQUE || rc == SQLITE_CONSTRAINT) {
      cmdb_error_set(err, CMDB_ERR_CONFLICT,
                     "That relationship already exists");
    } else {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot create relationship");
      cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
    }
    goto fail;
  }
  if (write_audit &&
      audit_relationship(db, "create", id, source_id, target_id, type_id,
                         effective_impact, err) != 0) {
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "rel_write", err) != 0) {
    return -1;
  }
  (void)cmdb_str_copy(out_id, 37, id);
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "rel_write", NULL);
  return -1;
}

int cmdb_relationship_create(cmdb_db_t *db, const char *source_id,
                             const char *target_id, const char *type_id,
                             const char *description, char out_id[37],
                             cmdb_error_t *err) {
  return cmdb_relationship_create_ex(db, source_id, target_id, type_id,
                                     description, NULL, 1, out_id, err);
}

int cmdb_relationship_delete(cmdb_db_t *db, const char *id,
                             cmdb_error_t *err) {
  cmdb_relationship_t existing;
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || id == NULL || !cmdb_uuid_valid(id)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid relationship ID");
    return -1;
  }
  if (cmdb_relationship_get(db, id, &existing, err) != 0) {
    return -1;
  }

  if (cmdb_db_savepoint_begin(db, "rel_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "DELETE FROM relationships WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare relationship delete");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot delete relationship");
    goto fail;
  }
  if (audit_relationship(db, "delete", id, existing.source_id,
                         existing.target_id, existing.type_id,
                         existing.impact, err) != 0) {
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "rel_write", err) != 0) {
    return -1;
  }
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "rel_write", NULL);
  return -1;
}

int cmdb_relationship_set_impact(cmdb_db_t *db, const char *id,
                                 const char *impact, cmdb_error_t *err) {
  cmdb_relationship_t existing;
  sqlite3_stmt *stmt = NULL;
  char now[21];
  int rc;

  if (db == NULL || id == NULL || !cmdb_uuid_valid(id) ||
      !cmdb_relationship_impact_valid(impact)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid relationship impact");
    return -1;
  }
  if (cmdb_relationship_get(db, id, &existing, err) != 0) {
    return -1;
  }
  if (strcmp(existing.impact, impact) == 0) {
    return 0;
  }
  cmdb_iso8601_now(now);
  if (cmdb_db_savepoint_begin(db, "rel_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "UPDATE relationships SET impact = ?, updated_at = ? "
                         "WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare relationship impact update");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, impact, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot update relationship impact");
    goto fail;
  }
  if (audit_relationship(db, "update", id, existing.source_id,
                         existing.target_id, existing.type_id, impact,
                         err) != 0) {
    goto fail;
  }
  return cmdb_db_savepoint_release(db, "rel_write", err);

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "rel_write", NULL);
  return -1;
}

int cmdb_relationship_get(cmdb_db_t *db, const char *id,
                          cmdb_relationship_t *out, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || id == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid relationship lookup");
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT r.id, r.source_id, s.name, r.target_id, "
                         "t.name, r.type_id, rt.name, r.description, "
                         "r.impact, r.created_at, r.updated_at "
                         "FROM relationships r "
                         "JOIN cis s ON s.id = r.source_id "
                         "JOIN cis t ON t.id = r.target_id "
                         "JOIN relationship_types rt ON rt.id = r.type_id "
                         "WHERE r.id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read relationship");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (read_rel_row(stmt, out) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL,
                     "Relationship value is too long");
      return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "Relationship not found");
    return -1;
  }
  cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read relationship");
  return -1;
}

int cmdb_relationship_list(cmdb_db_t *db, const char *ci_id, size_t limit,
                           size_t offset, cmdb_relationship_t **out,
                           size_t *count, size_t *total, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  cmdb_relationship_t *items;
  const char *filter;
  size_t total_value = 0;
  size_t i = 0;
  int rc;

  if (db == NULL || out == NULL || count == NULL || total == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid relationship list arguments");
    return -1;
  }
  *out = NULL;
  *count = 0;
  *total = 0;
  filter = ci_id != NULL ? ci_id : "";
  if (limit == 0 || limit > CMDB_MAX_LIST_LIMIT) {
    limit = limit == 0 ? 100 : CMDB_MAX_LIST_LIMIT;
  }

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT count(*) FROM relationships "
                         "WHERE (?1 = '' OR source_id = ?1 OR target_id = ?1);",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot count relationships");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, filter, -1, SQLITE_TRANSIENT);
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
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT r.id, r.source_id, s.name, r.target_id, "
                         "t.name, r.type_id, rt.name, r.description, "
                         "r.impact, r.created_at, r.updated_at "
                         "FROM relationships r "
                         "JOIN cis s ON s.id = r.source_id "
                         "JOIN cis t ON t.id = r.target_id "
                         "JOIN relationship_types rt ON rt.id = r.type_id "
                         "WHERE (?1 = '' OR r.source_id = ?1 OR r.target_id = ?1) "
                         "ORDER BY r.created_at, r.id LIMIT ?2 OFFSET ?3;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    free(items);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list relationships");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, filter, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, (sqlite3_int64)limit);
  sqlite3_bind_int64(stmt, 3, (sqlite3_int64)offset);
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW && i < limit) {
    if (read_rel_row(stmt, &items[i]) != 0) {
      sqlite3_finalize(stmt);
      free(items);
      cmdb_error_set(err, CMDB_ERR_INTERNAL,
                     "Relationship value is too long");
      return -1;
    }
    i++;
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
    free(items);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list relationships");
    return -1;
  }
  *out = items;
  *count = i;
  return 0;
}

void cmdb_relationship_free(cmdb_relationship_t *rel) {
  (void)rel;
}

void cmdb_relationship_free_list(cmdb_relationship_t *rels, size_t count) {
  (void)count;
  free(rels);
}
