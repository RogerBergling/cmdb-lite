#include "cmdb/audit.h"

#include "cmdb/crypto.h"
#include "cmdb/util.h"

#include <stdlib.h>
#include <string.h>

static char audit_actor[256] = "local";

void cmdb_audit_set_actor(const char *actor) {
  if (actor == NULL || actor[0] == '\0') {
    (void)cmdb_str_copy(audit_actor, sizeof(audit_actor), "local");
    return;
  }
  (void)cmdb_str_copy(audit_actor, sizeof(audit_actor), actor);
}

const char *cmdb_audit_actor(void) {
  return audit_actor;
}

static int get_previous_hash(cmdb_db_t *db, char out[65], cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  memset(out, '0', 64);
  out[64] = '\0';

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT hash FROM audit_log ORDER BY id DESC LIMIT 1;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read audit chain");
    return -1;
  }
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    const unsigned char *hash = sqlite3_column_text(stmt, 0);
    if (hash == NULL || strlen((const char *)hash) != 64) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Audit chain is corrupt");
      return -1;
    }
    memcpy(out, hash, 65);
  } else if (rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read audit chain");
    return -1;
  }
  sqlite3_finalize(stmt);
  return 0;
}

static int compute_audit_hash(const char *prev_hash, const char *ts,
                              const char *actor, const char *action,
                              const char *entity_type, const char *entity_id,
                              const char *detail, char out[65],
                              cmdb_error_t *err) {
  cmdb_strbuf_t sb;

  cmdb_strbuf_init(&sb);
  cmdb_strbuf_append(&sb, prev_hash);
  cmdb_strbuf_append_n(&sb, "\x1f", 1);
  cmdb_strbuf_append(&sb, ts);
  cmdb_strbuf_append_n(&sb, "\x1f", 1);
  cmdb_strbuf_append(&sb, actor);
  cmdb_strbuf_append_n(&sb, "\x1f", 1);
  cmdb_strbuf_append(&sb, action);
  cmdb_strbuf_append_n(&sb, "\x1f", 1);
  cmdb_strbuf_append(&sb, entity_type);
  cmdb_strbuf_append_n(&sb, "\x1f", 1);
  cmdb_strbuf_append(&sb, entity_id);
  cmdb_strbuf_append_n(&sb, "\x1f", 1);
  cmdb_strbuf_append(&sb, detail);

  if (cmdb_crypto_hash_hex(sb.data, sb.len, out, err) != 0) {
    cmdb_strbuf_free(&sb);
    return -1;
  }
  cmdb_strbuf_free(&sb);
  return 0;
}

int cmdb_audit_add(cmdb_db_t *db, const char *actor, const char *action,
                   const char *entity_type, const char *entity_id,
                   const char *detail_json, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char prev_hash[65];
  char hash[65];
  char ts[21];
  int rc;

  if (db == NULL || actor == NULL || action == NULL || entity_type == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid audit arguments");
    return -1;
  }
  if (entity_id == NULL) {
    entity_id = "";
  }
  if (detail_json == NULL) {
    detail_json = "";
  }

  if (get_previous_hash(db, prev_hash, err) != 0) {
    return -1;
  }
  cmdb_iso8601_now(ts);
  if (compute_audit_hash(prev_hash, ts, actor, action, entity_type,
                         entity_id, detail_json, hash, err) != 0) {
    return -1;
  }

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "INSERT INTO audit_log"
                         "(ts, actor, action, entity_type, entity_id, detail, "
                         "prev_hash, hash) VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare audit write");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, ts, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, actor, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, action, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, entity_type, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, entity_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, detail_json, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, prev_hash, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, hash, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot write audit entry");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
    return -1;
  }
  return 0;
}

int cmdb_audit_verify(cmdb_db_t *db, int *valid, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char expected_prev[65];
  int rc;

  if (db == NULL || valid == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid audit arguments");
    return -1;
  }
  *valid = 1;
  memset(expected_prev, '0', 64);
  expected_prev[64] = '\0';

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT ts, actor, action, entity_type, entity_id, "
                         "detail, prev_hash, hash FROM audit_log ORDER BY id;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read audit log");
    return -1;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const char *ts = (const char *)sqlite3_column_text(stmt, 0);
    const char *actor = (const char *)sqlite3_column_text(stmt, 1);
    const char *action = (const char *)sqlite3_column_text(stmt, 2);
    const char *entity_type = (const char *)sqlite3_column_text(stmt, 3);
    const char *entity_id = (const char *)sqlite3_column_text(stmt, 4);
    const char *detail = (const char *)sqlite3_column_text(stmt, 5);
    const char *prev_hash = (const char *)sqlite3_column_text(stmt, 6);
    const char *hash = (const char *)sqlite3_column_text(stmt, 7);
    char computed[65];

    if (ts == NULL || actor == NULL || action == NULL ||
        entity_type == NULL || entity_id == NULL || detail == NULL ||
        prev_hash == NULL || hash == NULL ||
        strcmp(prev_hash, expected_prev) != 0) {
      *valid = 0;
      break;
    }
    if (compute_audit_hash(prev_hash, ts, actor, action, entity_type,
                           entity_id, detail, computed, err) != 0) {
      sqlite3_finalize(stmt);
      return -1;
    }
    if (strcmp(computed, hash) != 0) {
      *valid = 0;
      break;
    }
    memcpy(expected_prev, hash, 65);
  }

  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read audit log");
    return -1;
  }
  return 0;
}

int cmdb_audit_list(cmdb_db_t *db, size_t limit, size_t offset,
                    cmdb_audit_entry_t **out, size_t *count, size_t *total,
                    cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  cmdb_audit_entry_t *items;
  size_t total_value = 0;
  size_t index = 0;
  int rc;

  if (db == NULL || out == NULL || count == NULL || total == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid audit list");
    return -1;
  }
  *out = NULL;
  *count = 0;
  *total = 0;
  if (limit == 0 || limit > CMDB_MAX_LIST_LIMIT) {
    limit = CMDB_MAX_LIST_LIMIT;
  }
  if (cmdb_audit_count(db, &total_value, err) != 0) {
    return -1;
  }
  *total = total_value;
  if (total_value == 0 || offset >= total_value) {
    return 0;
  }
  items = cmdb_calloc(limit, sizeof(*items));
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT id, ts, actor, action, entity_type, entity_id, detail, "
        "prev_hash, hash FROM audit_log ORDER BY id DESC LIMIT ? OFFSET ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    free(items);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list audit entries");
    return -1;
  }
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)limit);
  sqlite3_bind_int64(stmt, 2, (sqlite3_int64)offset);
  while (index < limit && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const char *text;
    items[index].id = sqlite3_column_int64(stmt, 0);
    text = (const char *)sqlite3_column_text(stmt, 1);
    (void)cmdb_str_copy(items[index].ts, sizeof(items[index].ts), text);
    text = (const char *)sqlite3_column_text(stmt, 2);
    (void)cmdb_str_copy(items[index].actor, sizeof(items[index].actor), text);
    text = (const char *)sqlite3_column_text(stmt, 3);
    (void)cmdb_str_copy(items[index].action, sizeof(items[index].action), text);
    text = (const char *)sqlite3_column_text(stmt, 4);
    (void)cmdb_str_copy(items[index].entity_type,
                        sizeof(items[index].entity_type), text);
    text = (const char *)sqlite3_column_text(stmt, 5);
    (void)cmdb_str_copy(items[index].entity_id,
                        sizeof(items[index].entity_id), text);
    text = (const char *)sqlite3_column_text(stmt, 6);
    items[index].detail = cmdb_strdup(text != NULL ? text : "");
    text = (const char *)sqlite3_column_text(stmt, 7);
    (void)cmdb_str_copy(items[index].prev_hash,
                        sizeof(items[index].prev_hash), text);
    text = (const char *)sqlite3_column_text(stmt, 8);
    (void)cmdb_str_copy(items[index].hash, sizeof(items[index].hash), text);
    index++;
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
    cmdb_audit_entries_free(items, index);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list audit entries");
    return -1;
  }
  *out = items;
  *count = index;
  return 0;
}

void cmdb_audit_entries_free(cmdb_audit_entry_t *entries, size_t count) {
  size_t i;
  if (entries == NULL) {
    return;
  }
  for (i = 0; i < count; i++) {
    free(entries[i].detail);
  }
  free(entries);
}

int cmdb_audit_count(cmdb_db_t *db, size_t *out, cmdb_error_t *err) {  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid audit arguments");
    return -1;
  }
  *out = 0;
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT count(*) FROM audit_log;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot count audit entries");
    return -1;
  }
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *out = (size_t)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot count audit entries");
  return -1;
}
