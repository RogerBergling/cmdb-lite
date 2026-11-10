#include "cmdb/categories.h"

#include "cmdb/audit.h"
#include "cmdb/util.h"

#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

static int category_validate(const char *name, const char *description,
                            const char *color, int sort_order,
                            cmdb_error_t *err) {
  size_t len;

  if (name == NULL || name[0] == '\0') {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Category name is required");
    return -1;
  }
  len = strlen(name);
  if (len >= sizeof(((cmdb_category_t *)0)->name)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Category name is too long");
    return -1;
  }
  if (!cmdb_utf8_valid(name, len) || cmdb_has_control_chars(name)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Category name is invalid");
    return -1;
  }
  if (description != NULL) {
    len = strlen(description);
    if (len >= sizeof(((cmdb_category_t *)0)->description) ||
        !cmdb_utf8_valid(description, len) ||
        cmdb_has_control_chars(description)) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "Category description is invalid or too long");
      return -1;
    }
  }
  if (color != NULL) {
    len = strlen(color);
    if (len >= sizeof(((cmdb_category_t *)0)->color) ||
        cmdb_has_control_chars(color)) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION, "Category color is invalid");
      return -1;
    }
  }
  if (sort_order < 0 || sort_order > 10000) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Category sort order is out of range");
    return -1;
  }
  return 0;
}

static int category_fill(sqlite3_stmt *stmt, cmdb_category_t *out) {
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
  text = (const char *)sqlite3_column_text(stmt, 2);
  if (cmdb_str_copy(out->description, sizeof(out->description), text) != 0) {
    return -1;
  }
  text = (const char *)sqlite3_column_text(stmt, 3);
  if (cmdb_str_copy(out->color, sizeof(out->color), text) != 0) {
    return -1;
  }
  out->sort_order = sqlite3_column_int(stmt, 4);
  text = (const char *)sqlite3_column_text(stmt, 5);
  if (cmdb_str_copy(out->created_at, sizeof(out->created_at), text) != 0) {
    return -1;
  }
  text = (const char *)sqlite3_column_text(stmt, 6);
  if (cmdb_str_copy(out->updated_at, sizeof(out->updated_at), text) != 0) {
    return -1;
  }
  return 0;
}

static int category_audit(cmdb_db_t *db, const char *action,
                          const char *id, const char *name,
                          cmdb_error_t *err) {
  cJSON *detail = cJSON_CreateObject();
  char *json;
  int rc;

  if (detail == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create category audit");
    return -1;
  }
  cJSON_AddStringToObject(detail, "name", name);
  json = cJSON_PrintUnformatted(detail);
  cJSON_Delete(detail);
  if (json == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create category audit");
    return -1;
  }
  rc = cmdb_audit_add(db, "local", action, "ci_category", id, json, err);
  free(json);
  return rc;
}

int cmdb_categories_list(cmdb_db_t *db, cmdb_category_t **out,
                         size_t *count, cmdb_error_t *err) {
  sqlite3_stmt *counter = NULL;
  sqlite3_stmt *stmt = NULL;
  cmdb_category_t *items;
  size_t total = 0;
  size_t index = 0;
  int rc;

  if (db == NULL || out == NULL || count == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid category list");
    return -1;
  }
  *out = NULL;
  *count = 0;
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT count(*) FROM ci_categories;", -1,
                         &counter, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot count categories");
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
        "SELECT id, name, description, color, sort_order, created_at, "
        "updated_at FROM ci_categories ORDER BY sort_order, name COLLATE NOCASE;",
        -1, &stmt, NULL) != SQLITE_OK) {
    free(items);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list categories");
    return -1;
  }
  while (index < total && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (category_fill(stmt, &items[index]) != 0) {
      sqlite3_finalize(stmt);
      free(items);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Category value is too long");
      return -1;
    }
    index++;
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
    free(items);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list categories");
    return -1;
  }
  *out = items;
  *count = index;
  return 0;
}

int cmdb_categories_get(cmdb_db_t *db, const char *id, cmdb_category_t *out,
                        cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || id == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid category lookup");
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT id, name, description, color, sort_order, created_at, "
        "updated_at FROM ci_categories WHERE id = ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read category");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (category_fill(stmt, out) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Category value is too long");
      return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  cmdb_error_set(err, rc == SQLITE_DONE ? CMDB_ERR_NOT_FOUND
                                        : CMDB_ERR_DATABASE,
                 rc == SQLITE_DONE ? "Category not found"
                                   : "Cannot read category");
  return -1;
}

int cmdb_categories_find_by_name(cmdb_db_t *db, const char *name,
                                 cmdb_category_t *out, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || name == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid category lookup");
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT id, name, description, color, sort_order, created_at, "
        "updated_at FROM ci_categories WHERE name = ? COLLATE NOCASE;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read category");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (category_fill(stmt, out) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Category value is too long");
      return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  cmdb_error_set(err, rc == SQLITE_DONE ? CMDB_ERR_NOT_FOUND
                                        : CMDB_ERR_DATABASE,
                 rc == SQLITE_DONE ? "Category not found"
                                   : "Cannot read category");
  return -1;
}

int cmdb_categories_create(cmdb_db_t *db, const char *name,
                           const char *description, const char *color,
                           int sort_order, char out_id[37],
                           cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char id[37];
  char now[21];
  int rc;

  if (db == NULL || out_id == NULL ||
      category_validate(name, description, color, sort_order, err) != 0) {
    return -1;
  }
  out_id[0] = '\0';
  if (cmdb_random_uuid(id) != 0) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot generate category ID");
    return -1;
  }
  cmdb_iso8601_now(now);
  if (cmdb_db_savepoint_begin(db, "category_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "INSERT INTO ci_categories(id, name, description, color, sort_order, "
        "created_at, updated_at) VALUES(?, ?, ?, ?, ?, ?, ?);",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare category insert");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, description != NULL ? description : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, color != NULL ? color : "", -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 5, sort_order);
  sqlite3_bind_text(stmt, 6, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, now, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_CONSTRAINT_UNIQUE || rc == SQLITE_CONSTRAINT) {
      cmdb_error_set(err, CMDB_ERR_CONFLICT,
                     "A category with that name already exists");
    } else {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot create category");
    }
    goto fail;
  }
  if (category_audit(db, "create", id, name, err) != 0) {
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "category_write", err) != 0) {
    return -1;
  }
  (void)cmdb_str_copy(out_id, 37, id);
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "category_write", NULL);
  return -1;
}

int cmdb_categories_update(cmdb_db_t *db, const char *id, const char *name,
                           const char *description, const char *color,
                           int sort_order, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char now[21];
  int rc;

  if (db == NULL || id == NULL ||
      category_validate(name, description, color, sort_order, err) != 0) {
    return -1;
  }
  cmdb_iso8601_now(now);
  if (cmdb_db_savepoint_begin(db, "category_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE ci_categories SET name = ?, description = ?, color = ?, "
        "sort_order = ?, updated_at = ? WHERE id = ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare category update");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, description != NULL ? description : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, color != NULL ? color : "", -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 4, sort_order);
  sqlite3_bind_text(stmt, 5, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_CONSTRAINT_UNIQUE || rc == SQLITE_CONSTRAINT) {
      cmdb_error_set(err, CMDB_ERR_CONFLICT,
                     "A category with that name already exists");
    } else {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot update category");
    }
    goto fail;
  }
  if (cmdb_db_changes(db) == 0) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "Category not found");
    goto fail;
  }
  if (category_audit(db, "update", id, name, err) != 0) {
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "category_write", err) != 0) {
    return -1;
  }
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "category_write", NULL);
  return -1;
}

int cmdb_categories_delete(cmdb_db_t *db, const char *id,
                           cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || id == NULL || !cmdb_uuid_valid(id)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid category ID");
    return -1;
  }
  if (cmdb_db_savepoint_begin(db, "category_write", err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "UPDATE ci_types SET category = '' "
                         "WHERE category = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot clear category assignments");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot clear category assignments");
    goto fail;
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "DELETE FROM ci_categories WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot delete category");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot delete category");
    goto fail;
  }
  if (cmdb_db_changes(db) == 0) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "Category not found");
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "category_write", err) != 0) {
    return -1;
  }
  (void)cmdb_audit_add(db, "local", "delete", "ci_category", id, "{}", NULL);
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "category_write", NULL);
  return -1;
}

int cmdb_categories_exists(cmdb_db_t *db, const char *id) {
  cmdb_category_t category;
  cmdb_error_t err;

  if (db == NULL || id == NULL || id[0] == '\0') {
    return 0;
  }
  cmdb_error_clear(&err);
  return cmdb_categories_get(db, id, &category, &err) == 0;
}

void cmdb_categories_free(cmdb_category_t *categories) {
  free(categories);
}
