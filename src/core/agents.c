#include "cmdb/agents.h"

#include "cmdb/crypto.h"
#include "cmdb/util.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void agent_str(char *dst, size_t dst_size, const char *src) {
  (void)cmdb_str_copy(dst, dst_size, src != NULL ? src : "");
}

static int agent_text_valid(const char *value, size_t max_len) {
  size_t len;

  if (value == NULL) {
    return 1;
  }
  len = strlen(value);
  if (len > max_len) {
    return 0;
  }
  return !cmdb_has_control_chars(value);
}

static int agent_token_format_valid(const char *token) {
  size_t i;

  if (token == NULL || strlen(token) != CMDB_AGENT_TOKEN_LEN) {
    return 0;
  }
  for (i = 0; i < CMDB_AGENT_TOKEN_LEN; i++) {
    char c = token[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) {
      return 0;
    }
  }
  return 1;
}

static int agent_time_plus(int seconds, char out[21]) {
  time_t when;
  struct tm tm_value;

  if (out == NULL) {
    return -1;
  }
  out[0] = '\0';
  when = time(NULL);
  if (when == (time_t)-1) {
    return -1;
  }
  when += (time_t)seconds;
#ifdef _WIN32
  if (gmtime_s(&tm_value, &when) != 0) {
    return -1;
  }
#else
  if (gmtime_r(&when, &tm_value) == NULL) {
    return -1;
  }
#endif
  if (strftime(out, 21, "%Y-%m-%dT%H:%M:%SZ", &tm_value) == 0) {
    out[0] = '\0';
    return -1;
  }
  return 0;
}

static int agent_new_token(char token[CMDB_AGENT_TOKEN_LEN + 1],
                           char hash[65], char prefix[9],
                           cmdb_error_t *err) {
  unsigned char raw[32];

  if (cmdb_crypto_random(raw, sizeof(raw), err) != 0) {
    return -1;
  }
  if (cmdb_hex_encode(raw, sizeof(raw), token,
                      CMDB_AGENT_TOKEN_LEN + 1) != 0) {
    cmdb_crypto_wipe(raw, sizeof(raw));
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot encode agent token");
    return -1;
  }
  cmdb_crypto_wipe(raw, sizeof(raw));
  if (cmdb_crypto_hash_hex(token, CMDB_AGENT_TOKEN_LEN, hash, err) != 0) {
    cmdb_crypto_wipe(token, CMDB_AGENT_TOKEN_LEN + 1);
    return -1;
  }
  memcpy(prefix, token, CMDB_AGENT_TOKEN_PREFIX_LEN);
  prefix[CMDB_AGENT_TOKEN_PREFIX_LEN] = '\0';
  return 0;
}

static int agent_facts_valid(const cmdb_agent_facts_t *facts,
                             cmdb_error_t *err) {
  if (facts == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Missing agent host facts");
    return -1;
  }
  if (!agent_text_valid(facts->display_name, 255) ||
      !agent_text_valid(facts->hostname, 255) ||
      !agent_text_valid(facts->machine_id, 127) ||
      !agent_text_valid(facts->os_family, 31) ||
      !agent_text_valid(facts->os_name, 127) ||
      !agent_text_valid(facts->os_version, 127) ||
      !agent_text_valid(facts->architecture, 31) ||
      !agent_text_valid(facts->agent_version, 63) ||
      !agent_text_valid(facts->last_seen_ip, 63)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Agent host facts contain invalid characters or are too long");
    return -1;
  }
  if (facts->display_name[0] == '\0' && facts->hostname[0] == '\0' &&
      facts->machine_id[0] == '\0') {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Agent host needs a hostname, display name, or machine ID");
    return -1;
  }
  return 0;
}

static int agent_app_valid(const cmdb_agent_app_t *app, cmdb_error_t *err) {
  if (app == NULL || app->app_key[0] == '\0' || app->name[0] == '\0') {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Application key and name are required");
    return -1;
  }
  if (!agent_text_valid(app->app_key, 255) ||
      !agent_text_valid(app->name, 511) ||
      !agent_text_valid(app->version, 255) ||
      !agent_text_valid(app->publisher, 255) ||
      !agent_text_valid(app->source, 31) ||
      !agent_text_valid(app->architecture, 31) ||
      !agent_text_valid(app->install_date, 31) ||
      !agent_text_valid(app->install_location, 1023)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Application fields contain invalid characters or are too long");
    return -1;
  }
  if (app->size_kb < 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Application size cannot be negative");
    return -1;
  }
  return 0;
}

static void agent_copy_row(sqlite3_stmt *stmt, cmdb_agent_t *out) {
  memset(out, 0, sizeof(*out));
  agent_str(out->id, sizeof(out->id),
            (const char *)sqlite3_column_text(stmt, 0));
  agent_str(out->display_name, sizeof(out->display_name),
            (const char *)sqlite3_column_text(stmt, 1));
  agent_str(out->hostname, sizeof(out->hostname),
            (const char *)sqlite3_column_text(stmt, 2));
  agent_str(out->machine_id, sizeof(out->machine_id),
            (const char *)sqlite3_column_text(stmt, 3));
  agent_str(out->os_family, sizeof(out->os_family),
            (const char *)sqlite3_column_text(stmt, 4));
  agent_str(out->os_name, sizeof(out->os_name),
            (const char *)sqlite3_column_text(stmt, 5));
  agent_str(out->os_version, sizeof(out->os_version),
            (const char *)sqlite3_column_text(stmt, 6));
  agent_str(out->architecture, sizeof(out->architecture),
            (const char *)sqlite3_column_text(stmt, 7));
  agent_str(out->agent_version, sizeof(out->agent_version),
            (const char *)sqlite3_column_text(stmt, 8));
  agent_str(out->status, sizeof(out->status),
            (const char *)sqlite3_column_text(stmt, 9));
  agent_str(out->enrolled_at, sizeof(out->enrolled_at),
            (const char *)sqlite3_column_text(stmt, 10));
  agent_str(out->last_report_at, sizeof(out->last_report_at),
            (const char *)sqlite3_column_text(stmt, 11));
  agent_str(out->last_seen_ip, sizeof(out->last_seen_ip),
            (const char *)sqlite3_column_text(stmt, 12));
  agent_str(out->ci_id, sizeof(out->ci_id),
            (const char *)sqlite3_column_text(stmt, 13));
  agent_str(out->ci_name, sizeof(out->ci_name),
            (const char *)sqlite3_column_text(stmt, 14));
  out->app_count = (size_t)sqlite3_column_int64(stmt, 15);
  out->report_count = (size_t)sqlite3_column_int64(stmt, 16);
  agent_str(out->created_at, sizeof(out->created_at),
            (const char *)sqlite3_column_text(stmt, 17));
  agent_str(out->updated_at, sizeof(out->updated_at),
            (const char *)sqlite3_column_text(stmt, 18));
}

static int agent_suggest_ci(cmdb_db_t *db, cmdb_agent_t *agent,
                            cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  size_t found = 0;
  int rc;

  if (agent->hostname[0] == '\0' || agent->ci_id[0] != '\0') {
    return 0;
  }
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT c.id, c.name FROM cis c "
        "WHERE c.name = ? COLLATE NOCASE "
        "UNION "
        "SELECT c.id, c.name FROM cis c "
        "JOIN ci_attributes a ON a.ci_id = c.id "
        "WHERE a.field_key = 'hostname' AND a.value = ? COLLATE NOCASE "
        "LIMIT 2;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent CI suggestion");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, agent->hostname, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, agent->hostname, -1, SQLITE_TRANSIENT);
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    found++;
    if (found == 1) {
      agent_str(agent->suggested_ci_id, sizeof(agent->suggested_ci_id),
                (const char *)sqlite3_column_text(stmt, 0));
      agent_str(agent->suggested_ci_name, sizeof(agent->suggested_ci_name),
                (const char *)sqlite3_column_text(stmt, 1));
    }
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot read agent CI suggestion");
    return -1;
  }
  if (found != 1) {
    agent->suggested_ci_id[0] = '\0';
    agent->suggested_ci_name[0] = '\0';
  }
  return 0;
}

int cmdb_agent_token_create(cmdb_db_t *db, const char *label,
                            const char *created_by, int ttl_seconds,
                            char token_out[CMDB_AGENT_TOKEN_LEN + 1],
                            cmdb_agent_token_t *out, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char id[37];
  char token[CMDB_AGENT_TOKEN_LEN + 1];
  char hash[65];
  char prefix[9];
  char now[21];
  int rc;

  if (db == NULL || token_out == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid enrollment-token arguments");
    return -1;
  }
  memset(out, 0, sizeof(*out));
  token_out[0] = '\0';
  if (ttl_seconds < 60 || ttl_seconds > 7 * 24 * 60 * 60) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Enrollment token lifetime must be between 60 seconds and 7 days");
    return -1;
  }
  if (!agent_text_valid(label, 127) ||
      !agent_text_valid(created_by, 63)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Enrollment token label or actor is invalid");
    return -1;
  }
  if (cmdb_random_uuid(id) != 0) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL,
                   "Cannot generate enrollment token ID");
    return -1;
  }
  if (agent_new_token(token, hash, prefix, err) != 0) {
    return -1;
  }
  cmdb_iso8601_now(now);
  if (agent_time_plus(ttl_seconds, out->expires_at) != 0) {
    cmdb_crypto_wipe(token, sizeof(token));
    cmdb_error_set(err, CMDB_ERR_INTERNAL,
                   "Cannot calculate enrollment token expiry");
    return -1;
  }
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "INSERT INTO agent_enrollment_tokens"
        "(id, token_hash, token_prefix, label, created_by, created_at, "
        " expires_at, used_at, revoked_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, '', '');",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_crypto_wipe(token, sizeof(token));
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare enrollment token");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, prefix, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, label != NULL ? label : "", -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, created_by != NULL ? created_by : "", -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, out->expires_at, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_crypto_wipe(token, sizeof(token));
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot create enrollment token");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
    return -1;
  }
  agent_str(out->id, sizeof(out->id), id);
  agent_str(out->token_prefix, sizeof(out->token_prefix), prefix);
  agent_str(out->label, sizeof(out->label), label);
  agent_str(out->created_by, sizeof(out->created_by), created_by);
  agent_str(out->created_at, sizeof(out->created_at), now);
  agent_str(token_out, CMDB_AGENT_TOKEN_LEN + 1, token);
  cmdb_crypto_wipe(token, sizeof(token));
  return 0;
}

int cmdb_agent_token_list(cmdb_db_t *db, cmdb_agent_token_t **out,
                          size_t *count, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  cmdb_agent_token_t *tokens = NULL;
  size_t used = 0;
  size_t capacity = 0;
  int rc;

  if (db == NULL || out == NULL || count == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid enrollment-token list arguments");
    return -1;
  }
  *out = NULL;
  *count = 0;
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT id, token_prefix, label, created_by, created_at, expires_at, "
        "       used_at, revoked_at "
        "FROM agent_enrollment_tokens ORDER BY created_at DESC, rowid DESC LIMIT 100;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare enrollment-token list");
    return -1;
  }
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cmdb_agent_token_t *token;
    if (used == capacity) {
      size_t next = capacity == 0 ? 8 : capacity * 2;
      tokens = cmdb_realloc(tokens, next * sizeof(*tokens));
      capacity = next;
    }
    token = &tokens[used++];
    memset(token, 0, sizeof(*token));
    agent_str(token->id, sizeof(token->id),
              (const char *)sqlite3_column_text(stmt, 0));
    agent_str(token->token_prefix, sizeof(token->token_prefix),
              (const char *)sqlite3_column_text(stmt, 1));
    agent_str(token->label, sizeof(token->label),
              (const char *)sqlite3_column_text(stmt, 2));
    agent_str(token->created_by, sizeof(token->created_by),
              (const char *)sqlite3_column_text(stmt, 3));
    agent_str(token->created_at, sizeof(token->created_at),
              (const char *)sqlite3_column_text(stmt, 4));
    agent_str(token->expires_at, sizeof(token->expires_at),
              (const char *)sqlite3_column_text(stmt, 5));
    agent_str(token->used_at, sizeof(token->used_at),
              (const char *)sqlite3_column_text(stmt, 6));
    agent_str(token->revoked_at, sizeof(token->revoked_at),
              (const char *)sqlite3_column_text(stmt, 7));
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    free(tokens);
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot read enrollment tokens");
    return -1;
  }
  *out = tokens;
  *count = used;
  return 0;
}

void cmdb_agent_tokens_free(cmdb_agent_token_t *tokens) {
  free(tokens);
}

int cmdb_agent_token_revoke(cmdb_db_t *db, const char *token_id,
                            cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char now[21];
  int rc;

  if (db == NULL || token_id == NULL || !cmdb_uuid_valid(token_id)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid enrollment token ID");
    return -1;
  }
  cmdb_iso8601_now(now);
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE agent_enrollment_tokens SET revoked_at = ? "
        "WHERE id = ? AND used_at = '' AND revoked_at = '';",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare enrollment token revoke");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, token_id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot revoke enrollment token");
    return -1;
  }
  if (cmdb_db_changes(db) == 0) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND,
                   "Enrollment token is unknown, used, or already revoked");
    return -1;
  }
  return 0;
}

static int agent_token_lookup(cmdb_db_t *db, const char *token,
                              char token_id[37], cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char hash[65];
  char now[21];
  int rc;

  if (!agent_token_format_valid(token)) {
    cmdb_error_set(err, CMDB_ERR_AUTH,
                   "Invalid or expired enrollment token");
    return -1;
  }
  if (cmdb_crypto_hash_hex(token, CMDB_AGENT_TOKEN_LEN, hash, err) != 0) {
    return -1;
  }
  cmdb_iso8601_now(now);
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT id, expires_at, used_at, revoked_at "
        "FROM agent_enrollment_tokens WHERE token_hash = ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare enrollment token lookup");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW ||
      sqlite3_column_text(stmt, 2) == NULL ||
      ((const char *)sqlite3_column_text(stmt, 2))[0] != '\0' ||
      sqlite3_column_text(stmt, 3) == NULL ||
      ((const char *)sqlite3_column_text(stmt, 3))[0] != '\0' ||
      sqlite3_column_text(stmt, 1) == NULL ||
      strcmp((const char *)sqlite3_column_text(stmt, 1), now) <= 0) {
    sqlite3_finalize(stmt);
    cmdb_error_set(err, CMDB_ERR_AUTH,
                   "Invalid or expired enrollment token");
    return -1;
  }
  agent_str(token_id, 37, (const char *)sqlite3_column_text(stmt, 0));
  sqlite3_finalize(stmt);
  return 0;
}

static int agent_find_by_machine(cmdb_db_t *db, const char *machine_id,
                                 char agent_id[37], cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  agent_id[0] = '\0';
  if (machine_id == NULL || machine_id[0] == '\0') {
    return 0;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT id FROM agents WHERE machine_id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent machine lookup");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, machine_id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    agent_str(agent_id, 37, (const char *)sqlite3_column_text(stmt, 0));
  } else if (rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot read agent machine lookup");
    return -1;
  }
  sqlite3_finalize(stmt);
  return 0;
}

int cmdb_agent_enroll(cmdb_db_t *db, const char *enrollment_token,
                      const cmdb_agent_facts_t *facts,
                      char agent_token_out[CMDB_AGENT_TOKEN_LEN + 1],
                      cmdb_agent_t *out, int *reused, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char token_id[37];
  char existing_id[37];
  char agent_id[37];
  char agent_token[CMDB_AGENT_TOKEN_LEN + 1];
  char agent_hash[65];
  char agent_prefix[9];
  char now[21];
  char display_name[256];
  const char *effective_name;
  int rc;
  int is_reused = 0;

  if (db == NULL || facts == NULL || out == NULL || agent_token_out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid agent enrollment arguments");
    return -1;
  }
  memset(out, 0, sizeof(*out));
  agent_token_out[0] = '\0';
  if (reused != NULL) {
    *reused = 0;
  }
  if (agent_facts_valid(facts, err) != 0 ||
      agent_token_lookup(db, enrollment_token, token_id, err) != 0) {
    return -1;
  }
  if (agent_find_by_machine(db, facts->machine_id, existing_id, err) != 0) {
    return -1;
  }
  agent_str(display_name, sizeof(display_name), facts->display_name);
  effective_name = display_name;
  if (effective_name[0] == '\0') {
    effective_name = facts->hostname;
  }
  if (effective_name[0] == '\0') {
    effective_name = facts->machine_id;
  }
  if (agent_new_token(agent_token, agent_hash, agent_prefix, err) != 0) {
    return -1;
  }
  cmdb_iso8601_now(now);

  if (cmdb_db_begin(db, err) != 0) {
    cmdb_crypto_wipe(agent_token, sizeof(agent_token));
    return -1;
  }
  /*
   * Consume the one-time token atomically. The read above produces a useful
   * error, but this conditional update is what prevents two concurrent
   * enrollments from using the same token.
   */
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE agent_enrollment_tokens SET used_at = ? "
        "WHERE id = ? AND used_at = '' AND revoked_at = '' "
        "AND expires_at > ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare enrollment token use");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, token_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, now, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE || cmdb_db_changes(db) != 1) {
    cmdb_error_set(err, CMDB_ERR_AUTH,
                   "Invalid or expired enrollment token");
    goto fail;
  }
  if (existing_id[0] != '\0') {
    agent_str(agent_id, sizeof(agent_id), existing_id);
    if (sqlite3_prepare_v2(
          cmdb_db_sqlite(db),
          "UPDATE agents SET display_name = ?, hostname = ?, machine_id = ?, "
          "  os_family = ?, os_name = ?, os_version = ?, architecture = ?, "
          "  agent_version = ?, token_hash = ?, token_prefix = ?, "
          "  status = 'active', last_seen_ip = ?, updated_at = ? "
          "WHERE id = ?;",
          -1, &stmt, NULL) != SQLITE_OK) {
      cmdb_error_set(err, CMDB_ERR_DATABASE,
                     "Cannot prepare agent enrollment update");
      goto fail;
    }
    sqlite3_bind_text(stmt, 1, effective_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, facts->hostname, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, facts->machine_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, facts->os_family, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, facts->os_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, facts->os_version, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, facts->architecture, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, facts->agent_version, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, agent_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, agent_prefix, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, facts->last_seen_ip, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, agent_id, -1, SQLITE_TRANSIENT);
    is_reused = 1;
  } else {
    if (cmdb_random_uuid(agent_id) != 0) {
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot generate agent ID");
      goto fail;
    }
    if (sqlite3_prepare_v2(
          cmdb_db_sqlite(db),
          "INSERT INTO agents"
          "(id, display_name, hostname, machine_id, os_family, os_name, "
          " os_version, architecture, agent_version, token_hash, token_prefix, "
          " status, enrolled_at, last_report_at, last_seen_ip, ci_id, "
          " created_at, updated_at) "
          "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'active', ?, '', ?, '', "
          "        ?, ?);",
          -1, &stmt, NULL) != SQLITE_OK) {
      cmdb_error_set(err, CMDB_ERR_DATABASE,
                     "Cannot prepare agent enrollment insert");
      goto fail;
    }
    sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, effective_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, facts->hostname, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, facts->machine_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, facts->os_family, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, facts->os_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, facts->os_version, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, facts->architecture, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, facts->agent_version, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, agent_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, agent_prefix, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, facts->last_seen_ip, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 15, now, -1, SQLITE_TRANSIENT);
  }
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_CONSTRAINT) {
      cmdb_error_set(err, CMDB_ERR_CONFLICT,
                     "Agent machine ID or token conflicts with an existing agent");
    } else {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot enroll agent");
      cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
    }
    goto fail;
  }
  if (cmdb_db_commit(db, err) != 0) {
    cmdb_crypto_wipe(agent_token, sizeof(agent_token));
    return -1;
  }
  agent_str(agent_token_out, CMDB_AGENT_TOKEN_LEN + 1, agent_token);
  cmdb_crypto_wipe(agent_token, sizeof(agent_token));
  if (reused != NULL) {
    *reused = is_reused;
  }
  if (cmdb_agent_get(db, agent_id, out, err) != 0) {
    cmdb_crypto_wipe(agent_token_out, CMDB_AGENT_TOKEN_LEN + 1);
    return -1;
  }
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_rollback(db, NULL);
  cmdb_crypto_wipe(agent_token, sizeof(agent_token));
  return -1;
}

int cmdb_agent_auth(cmdb_db_t *db, const char *agent_token,
                    cmdb_agent_t *out, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char hash[65];
  int rc;

  if (db == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid agent authentication arguments");
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (!agent_token_format_valid(agent_token)) {
    cmdb_error_set(err, CMDB_ERR_AUTH, "Invalid agent token");
    return -1;
  }
  if (cmdb_crypto_hash_hex(agent_token, CMDB_AGENT_TOKEN_LEN, hash, err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT " "a.id, a.display_name, a.hostname, a.machine_id, a.os_family, "
        "       a.os_name, a.os_version, a.architecture, a.agent_version, "
        "       a.status, a.enrolled_at, a.last_report_at, a.last_seen_ip, "
        "       a.ci_id, COALESCE(c.name, ''), "
        "       (SELECT COUNT(*) FROM agent_apps ap "
        "         WHERE ap.agent_id = a.id AND ap.removed_at = ''), "
        "       (SELECT COUNT(*) FROM agent_reports r WHERE r.agent_id = a.id), "
        "       a.created_at, a.updated_at "
        "FROM agents a LEFT JOIN cis c ON c.id = a.ci_id "
        "WHERE a.token_hash = ? AND a.status = 'active';",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent authentication");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    cmdb_error_set(err, CMDB_ERR_AUTH, "Invalid agent token");
    return -1;
  }
  agent_copy_row(stmt, out);
  sqlite3_finalize(stmt);
  if (agent_suggest_ci(db, out, err) != 0) {
    return -1;
  }
  return 0;
}

static int agent_app_changed(const cmdb_agent_app_t *incoming,
                             sqlite3_stmt *stmt) {
  const char *old_name = (const char *)sqlite3_column_text(stmt, 2);
  const char *old_version = (const char *)sqlite3_column_text(stmt, 3);
  const char *old_publisher = (const char *)sqlite3_column_text(stmt, 4);
  const char *old_source = (const char *)sqlite3_column_text(stmt, 5);
  const char *old_arch = (const char *)sqlite3_column_text(stmt, 6);
  const char *old_date = (const char *)sqlite3_column_text(stmt, 7);
  const char *old_location = (const char *)sqlite3_column_text(stmt, 8);
  long long old_size = sqlite3_column_int64(stmt, 9);

  return strcmp(incoming->name, old_name != NULL ? old_name : "") != 0 ||
         strcmp(incoming->version, old_version != NULL ? old_version : "") != 0 ||
         strcmp(incoming->publisher, old_publisher != NULL ? old_publisher : "") != 0 ||
         strcmp(incoming->source, old_source != NULL ? old_source : "") != 0 ||
         strcmp(incoming->architecture, old_arch != NULL ? old_arch : "") != 0 ||
         strcmp(incoming->install_date, old_date != NULL ? old_date : "") != 0 ||
         strcmp(incoming->install_location,
                old_location != NULL ? old_location : "") != 0 ||
         incoming->size_kb != old_size;
}

static char *agent_change_detail(const cmdb_agent_app_t *incoming,
                                 sqlite3_stmt *stmt) {
  cJSON *root = cJSON_CreateObject();
  cJSON *fields = cJSON_CreateArray();
  cJSON *old_values = cJSON_CreateObject();
  cJSON *new_values = cJSON_CreateObject();
  char *printed;

  if (root == NULL || fields == NULL || old_values == NULL || new_values == NULL) {
    cJSON_Delete(root);
    cJSON_Delete(fields);
    cJSON_Delete(old_values);
    cJSON_Delete(new_values);
    return NULL;
  }
#define AGENT_CHANGE_FIELD(field_name, column, value) \
  do { \
    const char *old_value = (const char *)sqlite3_column_text(stmt, (column)); \
    const char *new_value = (value); \
    if (strcmp(old_value != NULL ? old_value : "", new_value) != 0) { \
      cJSON_AddItemToArray(fields, cJSON_CreateString(field_name)); \
      cJSON_AddStringToObject(old_values, field_name, \
                              old_value != NULL ? old_value : ""); \
      cJSON_AddStringToObject(new_values, field_name, new_value); \
    } \
  } while (0)

  AGENT_CHANGE_FIELD("name", 2, incoming->name);
  AGENT_CHANGE_FIELD("version", 3, incoming->version);
  AGENT_CHANGE_FIELD("publisher", 4, incoming->publisher);
  AGENT_CHANGE_FIELD("source", 5, incoming->source);
  AGENT_CHANGE_FIELD("architecture", 6, incoming->architecture);
  AGENT_CHANGE_FIELD("install_date", 7, incoming->install_date);
  AGENT_CHANGE_FIELD("install_location", 8, incoming->install_location);
#undef AGENT_CHANGE_FIELD
  if (sqlite3_column_int64(stmt, 9) != incoming->size_kb) {
    cJSON_AddItemToArray(fields, cJSON_CreateString("size_kb"));
    cJSON_AddNumberToObject(old_values, "size_kb",
                            (double)sqlite3_column_int64(stmt, 9));
    cJSON_AddNumberToObject(new_values, "size_kb",
                            (double)incoming->size_kb);
  }
  cJSON_AddItemToObject(root, "fields", fields);
  cJSON_AddItemToObject(root, "old", old_values);
  cJSON_AddItemToObject(root, "new", new_values);
  printed = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return printed;
}

static int agent_insert_event(cmdb_db_t *db, const char *agent_id,
                              const char *report_id, const char *app_key,
                              const char *event_type, const char *name,
                              const char *old_version, const char *new_version,
                              const char *detail, const char *when,
                              cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "INSERT INTO agent_app_events"
        "(agent_id, report_id, app_key, event_type, name, old_version, "
        " new_version, detail, occurred_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?);",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare application change event");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, report_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, app_key, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, event_type, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, old_version, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, new_version, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, detail, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 9, when, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot store application change event");
    return -1;
  }
  return 0;
}

static int agent_app_key_compare(const void *left, const void *right) {
  const char *a = *(const char *const *)left;
  const char *b = *(const char *const *)right;
  return strcmp(a, b);
}

static int agent_prune_one(cmdb_db_t *db, const char *agent_id,
                           int retention_days, int max_reports,
                           size_t *deleted, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;
  int changes = 0;

  if (retention_days <= 0) {
    retention_days = CMDB_AGENT_RETENTION_DAYS;
  }
  if (retention_days < 1) {
    retention_days = 1;
  }
  if (retention_days > 3650) {
    retention_days = 3650;
  }
  if (max_reports <= 0) {
    max_reports = CMDB_AGENT_RETENTION_REPORTS;
  }
  if (max_reports < 1) {
    max_reports = 1;
  }
  if (max_reports > 3650) {
    max_reports = 3650;
  }
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "DELETE FROM agent_reports WHERE agent_id = ? AND "
        "julianday(received_at) < julianday('now') - ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent report age pruning");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, retention_days);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prune old agent reports");
    return -1;
  }
  changes += cmdb_db_changes(db);

  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "DELETE FROM agent_reports WHERE agent_id = ? AND id NOT IN "
        "(SELECT id FROM agent_reports WHERE agent_id = ? "
        " ORDER BY received_at DESC, rowid DESC LIMIT ?);",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent report count pruning");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, agent_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 3, max_reports);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prune excess agent reports");
    return -1;
  }
  changes += cmdb_db_changes(db);
  if (deleted != NULL) {
    *deleted += (size_t)changes;
  }
  return 0;
}

int cmdb_agent_report(cmdb_db_t *db, const char *agent_id,
                      const cmdb_agent_facts_t *facts,
                      const char *collected_at,
                      const cmdb_agent_app_t *apps, size_t app_count,
                      const char *collector_errors, int complete,
                      int retention_days, int max_reports,
                      cmdb_agent_report_t *out, cmdb_error_t *err) {
  sqlite3_stmt *lookup = NULL;
  sqlite3_stmt *insert = NULL;
  sqlite3_stmt *update = NULL;
  sqlite3_stmt *touch = NULL;
  sqlite3_stmt *stmt = NULL;
  const char **keys = NULL;
  char report_id[37];
  char now[21];
  char collected[21];
  char *detail = NULL;
  size_t added = 0;
  size_t removed = 0;
  size_t updated = 0;
  size_t i;
  int rc;

  if (db == NULL || agent_id == NULL || out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid agent report arguments");
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (!cmdb_uuid_valid(agent_id)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Invalid agent ID");
    return -1;
  }
  if (facts == NULL || agent_facts_valid(facts, err) != 0) {
    return -1;
  }
  if (apps == NULL && app_count > 0) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Missing application list");
    return -1;
  }
  if (app_count > CMDB_AGENT_MAX_APPS) {
    cmdb_error_set(err, CMDB_ERR_LIMIT,
                   "Application report exceeds the limit");
    return -1;
  }
  if (!agent_text_valid(collector_errors, 2047)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Collector error detail is invalid");
    return -1;
  }
  if (collected_at != NULL && collected_at[0] != '\0') {
    if (!cmdb_iso8601_valid(collected_at)) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "collected_at must be an ISO 8601 UTC timestamp");
      return -1;
    }
    agent_str(collected, sizeof(collected), collected_at);
  } else {
    cmdb_iso8601_now(collected);
  }
  keys = cmdb_malloc((app_count > 0 ? app_count : 1) * sizeof(*keys));
  for (i = 0; i < app_count; i++) {
    if (agent_app_valid(&apps[i], err) != 0) {
      free(keys);
      return -1;
    }
    keys[i] = apps[i].app_key;
  }
  if (app_count > 1) {
    qsort(keys, app_count, sizeof(*keys), agent_app_key_compare);
    for (i = 1; i < app_count; i++) {
      if (strcmp(keys[i - 1], keys[i]) == 0) {
        free(keys);
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Application report contains duplicate keys");
        return -1;
      }
    }
  }
  free(keys);

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT 1 FROM agents WHERE id = ? AND status = 'active';",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent report authentication");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_ROW) {
    cmdb_error_set(err, CMDB_ERR_AUTH, "Agent is unknown or revoked");
    return -1;
  }

  if (cmdb_random_uuid(report_id) != 0) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL,
                   "Cannot generate agent report ID");
    return -1;
  }
  cmdb_iso8601_now(now);
  if (cmdb_db_begin(db, err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "INSERT INTO agent_reports"
        "(id, agent_id, collected_at, received_at, app_count, added_count, "
        " removed_count, updated_count, complete, collector_errors) "
        "VALUES(?, ?, ?, ?, ?, 0, 0, 0, ?, ?);",
        -1, &insert, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent report");
    goto fail;
  }
  sqlite3_bind_text(insert, 1, report_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert, 2, agent_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert, 3, collected, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert, 4, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(insert, 5, (int)app_count);
  sqlite3_bind_int(insert, 6, complete ? 1 : 0);
  sqlite3_bind_text(insert, 7,
                    collector_errors != NULL ? collector_errors : "",
                    -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(insert);
  sqlite3_finalize(insert);
  insert = NULL;
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot create agent report");
    goto fail;
  }

  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT removed_at, name, version, publisher, source, architecture, "
        "       install_date, install_location, size_kb "
        "FROM agent_apps WHERE agent_id = ? AND app_key = ?;",
        -1, &lookup, NULL) != SQLITE_OK ||
      sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "INSERT INTO agent_apps"
        "(agent_id, app_key, name, version, publisher, source, architecture, "
        " install_date, install_location, size_kb, first_seen_at, "
        " last_seen_at, last_report_id, removed_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, '');",
        -1, &insert, NULL) != SQLITE_OK ||
      sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE agent_apps SET name = ?, version = ?, publisher = ?, "
        " source = ?, architecture = ?, install_date = ?, "
        " install_location = ?, size_kb = ?, last_seen_at = ?, "
        " last_report_id = ?, removed_at = '' "
        "WHERE agent_id = ? AND app_key = ?;",
        -1, &update, NULL) != SQLITE_OK ||
      sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE agent_apps SET last_seen_at = ?, last_report_id = ? "
        "WHERE agent_id = ? AND app_key = ?;",
        -1, &touch, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent application diff");
    goto fail;
  }
  for (i = 0; i < app_count; i++) {
    const cmdb_agent_app_t *app = &apps[i];
    int existed = 0;
    int was_removed = 0;
    int changed = 0;
    const char *old_version = "";

    sqlite3_reset(lookup);
    sqlite3_clear_bindings(lookup);
    sqlite3_bind_text(lookup, 1, agent_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(lookup, 2, app->app_key, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(lookup);
    if (rc == SQLITE_ROW) {
      const char *removed_at = (const char *)sqlite3_column_text(lookup, 0);
      const char *version = (const char *)sqlite3_column_text(lookup, 2);
      existed = 1;
      was_removed = removed_at != NULL && removed_at[0] != '\0';
      old_version = version != NULL ? version : "";
      changed = !was_removed && agent_app_changed(app, lookup);
      if (changed) {
        detail = agent_change_detail(app, lookup);
      }
    } else if (rc != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE,
                     "Cannot read current application state");
      goto fail;
    }

    if (!existed) {
      sqlite3_reset(insert);
      sqlite3_clear_bindings(insert);
      sqlite3_bind_text(insert, 1, agent_id, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 2, app->app_key, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 3, app->name, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 4, app->version, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 5, app->publisher, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 6, app->source, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 7, app->architecture, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 8, app->install_date, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 9, app->install_location, -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(insert, 10, app->size_kb);
      sqlite3_bind_text(insert, 11, now, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 12, now, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 13, report_id, -1, SQLITE_TRANSIENT);
      if (sqlite3_step(insert) != SQLITE_DONE) {
        cmdb_error_set(err, CMDB_ERR_DATABASE,
                       "Cannot store application inventory");
        goto fail;
      }
      if (agent_insert_event(db, agent_id, report_id, app->app_key,
                             "added", app->name, "", app->version, "{}",
                             now, err) != 0) {
        goto fail;
      }
      added++;
    } else if (was_removed) {
      sqlite3_reset(update);
      sqlite3_clear_bindings(update);
      sqlite3_bind_text(update, 1, app->name, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 2, app->version, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 3, app->publisher, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 4, app->source, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 5, app->architecture, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 6, app->install_date, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 7, app->install_location, -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_int64(update, 8, app->size_kb);
      sqlite3_bind_text(update, 9, now, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 10, report_id, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 11, agent_id, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 12, app->app_key, -1, SQLITE_TRANSIENT);
      if (sqlite3_step(update) != SQLITE_DONE) {
        cmdb_error_set(err, CMDB_ERR_DATABASE,
                       "Cannot restore application inventory");
        goto fail;
      }
      if (agent_insert_event(db, agent_id, report_id, app->app_key,
                             "added", app->name, "", app->version, "{}",
                             now, err) != 0) {
        goto fail;
      }
      added++;
    } else if (changed) {
      sqlite3_reset(update);
      sqlite3_clear_bindings(update);
      sqlite3_bind_text(update, 1, app->name, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 2, app->version, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 3, app->publisher, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 4, app->source, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 5, app->architecture, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 6, app->install_date, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 7, app->install_location, -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_int64(update, 8, app->size_kb);
      sqlite3_bind_text(update, 9, now, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 10, report_id, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 11, agent_id, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update, 12, app->app_key, -1, SQLITE_TRANSIENT);
      if (sqlite3_step(update) != SQLITE_DONE) {
        cmdb_error_set(err, CMDB_ERR_DATABASE,
                       "Cannot update application inventory");
        goto fail;
      }
      if (agent_insert_event(db, agent_id, report_id, app->app_key,
                             "updated", app->name, old_version, app->version,
                             detail != NULL ? detail : "{}", now, err) != 0) {
        goto fail;
      }
      updated++;
    } else {
      sqlite3_reset(touch);
      sqlite3_clear_bindings(touch);
      sqlite3_bind_text(touch, 1, now, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(touch, 2, report_id, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(touch, 3, agent_id, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(touch, 4, app->app_key, -1, SQLITE_TRANSIENT);
      if (sqlite3_step(touch) != SQLITE_DONE) {
        cmdb_error_set(err, CMDB_ERR_DATABASE,
                       "Cannot touch application inventory");
        goto fail;
      }
    }
    if (detail != NULL) {
      free(detail);
      detail = NULL;
    }
  }

  if (complete) {
    if (sqlite3_prepare_v2(
          cmdb_db_sqlite(db),
          "INSERT INTO agent_app_events"
          "(agent_id, report_id, app_key, event_type, name, old_version, "
          " new_version, detail, occurred_at) "
          "SELECT agent_id, ?, app_key, 'removed', name, version, '', '{}', ? "
          "FROM agent_apps WHERE agent_id = ? AND removed_at = '' "
          "AND last_report_id <> ?;",
          -1, &stmt, NULL) != SQLITE_OK) {
      cmdb_error_set(err, CMDB_ERR_DATABASE,
                     "Cannot prepare removed application events");
      goto fail;
    }
    sqlite3_bind_text(stmt, 1, report_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, agent_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, report_id, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE,
                     "Cannot record removed applications");
      goto fail;
    }
    removed = (size_t)cmdb_db_changes(db);

    if (sqlite3_prepare_v2(
          cmdb_db_sqlite(db),
          "UPDATE agent_apps SET removed_at = ? "
          "WHERE agent_id = ? AND removed_at = '' AND last_report_id <> ?;",
          -1, &stmt, NULL) != SQLITE_OK) {
      cmdb_error_set(err, CMDB_ERR_DATABASE,
                     "Cannot prepare removed application update");
      goto fail;
    }
    sqlite3_bind_text(stmt, 1, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, agent_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, report_id, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE,
                     "Cannot mark removed applications");
      goto fail;
    }
  }

  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE agent_reports SET added_count = ?, removed_count = ?, "
        "updated_count = ? WHERE id = ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent report summary");
    goto fail;
  }
  sqlite3_bind_int(stmt, 1, (int)added);
  sqlite3_bind_int(stmt, 2, (int)removed);
  sqlite3_bind_int(stmt, 3, (int)updated);
  sqlite3_bind_text(stmt, 4, report_id, -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot update agent report summary");
    goto fail;
  }
  sqlite3_finalize(stmt);
  stmt = NULL;

  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE agents SET display_name = ?, hostname = ?, os_family = ?, "
        " os_name = ?, os_version = ?, architecture = ?, agent_version = ?, "
        " last_report_at = ?, last_seen_ip = ?, updated_at = ? WHERE id = ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent report metadata update");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1,
                    facts->display_name[0] != '\0' ? facts->display_name
                                                   : facts->hostname,
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, facts->hostname, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, facts->os_family, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, facts->os_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, facts->os_version, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, facts->architecture, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, facts->agent_version, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 9, facts->last_seen_ip, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 10, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 11, agent_id, -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot update agent report metadata");
    goto fail;
  }
  sqlite3_finalize(stmt);
  stmt = NULL;

  if (agent_prune_one(db, agent_id, retention_days, max_reports, NULL,
                      err) != 0) {
    goto fail;
  }
  sqlite3_finalize(lookup);
  sqlite3_finalize(insert);
  sqlite3_finalize(update);
  sqlite3_finalize(touch);
  lookup = NULL;
  insert = NULL;
  update = NULL;
  touch = NULL;
  if (cmdb_db_commit(db, err) != 0) {
    (void)cmdb_db_rollback(db, NULL);
    return -1;
  }
  agent_str(out->id, sizeof(out->id), report_id);
  agent_str(out->collected_at, sizeof(out->collected_at), collected);
  agent_str(out->received_at, sizeof(out->received_at), now);
  out->app_count = app_count;
  out->added_count = added;
  out->removed_count = removed;
  out->updated_count = updated;
  out->complete = complete ? 1 : 0;
  agent_str(out->collector_errors, sizeof(out->collector_errors),
            collector_errors);
  return 0;

fail:
  if (lookup != NULL) {
    sqlite3_finalize(lookup);
  }
  if (insert != NULL) {
    sqlite3_finalize(insert);
  }
  if (update != NULL) {
    sqlite3_finalize(update);
  }
  if (touch != NULL) {
    sqlite3_finalize(touch);
  }
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  free(detail);
  (void)cmdb_db_rollback(db, NULL);
  return -1;
}

int cmdb_agent_list(cmdb_db_t *db, cmdb_agent_t **out, size_t *count,
                    cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  cmdb_agent_t *agents = NULL;
  size_t used = 0;
  size_t capacity = 0;
  int rc;

  if (db == NULL || out == NULL || count == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid agent list arguments");
    return -1;
  }
  *out = NULL;
  *count = 0;
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT a.id, a.display_name, a.hostname, a.machine_id, a.os_family, "
        "       a.os_name, a.os_version, a.architecture, a.agent_version, "
        "       a.status, a.enrolled_at, a.last_report_at, a.last_seen_ip, "
        "       a.ci_id, COALESCE(c.name, ''), "
        "       (SELECT COUNT(*) FROM agent_apps ap "
        "         WHERE ap.agent_id = a.id AND ap.removed_at = ''), "
        "       (SELECT COUNT(*) FROM agent_reports r WHERE r.agent_id = a.id), "
        "       a.created_at, a.updated_at "
        "FROM agents a LEFT JOIN cis c ON c.id = a.ci_id "
        "ORDER BY a.last_report_at DESC, a.display_name COLLATE NOCASE;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare agent list");
    return -1;
  }
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (used == capacity) {
      size_t next = capacity == 0 ? 8 : capacity * 2;
      agents = cmdb_realloc(agents, next * sizeof(*agents));
      capacity = next;
    }
    agent_copy_row(stmt, &agents[used]);
    used++;
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    free(agents);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read agents");
    return -1;
  }
  for (size_t i = 0; i < used; i++) {
    if (agent_suggest_ci(db, &agents[i], err) != 0) {
      free(agents);
      return -1;
    }
  }
  *out = agents;
  *count = used;
  return 0;
}

int cmdb_agent_get(cmdb_db_t *db, const char *agent_id, cmdb_agent_t *out,
                   cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char id_copy[37];
  int rc;

  if (db == NULL || out == NULL || !cmdb_uuid_valid(agent_id)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid agent ID");
    return -1;
  }
  agent_str(id_copy, sizeof(id_copy), agent_id);
  memset(out, 0, sizeof(*out));
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT a.id, a.display_name, a.hostname, a.machine_id, a.os_family, "
        "       a.os_name, a.os_version, a.architecture, a.agent_version, "
        "       a.status, a.enrolled_at, a.last_report_at, a.last_seen_ip, "
        "       a.ci_id, COALESCE(c.name, ''), "
        "       (SELECT COUNT(*) FROM agent_apps ap "
        "         WHERE ap.agent_id = a.id AND ap.removed_at = ''), "
        "       (SELECT COUNT(*) FROM agent_reports r WHERE r.agent_id = a.id), "
        "       a.created_at, a.updated_at "
        "FROM agents a LEFT JOIN cis c ON c.id = a.ci_id WHERE a.id = ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare agent lookup");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id_copy, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "Agent not found");
    return -1;
  }
  agent_copy_row(stmt, out);
  sqlite3_finalize(stmt);
  return agent_suggest_ci(db, out, err);
}

int cmdb_agent_apps(cmdb_db_t *db, const char *agent_id,
                    cmdb_agent_app_t **out, size_t *count, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  cmdb_agent_app_t *apps = NULL;
  size_t used = 0;
  size_t capacity = 0;
  int rc;

  if (db == NULL || out == NULL || count == NULL ||
      !cmdb_uuid_valid(agent_id)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid agent application list arguments");
    return -1;
  }
  *out = NULL;
  *count = 0;
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT app_key, name, version, publisher, source, architecture, "
        "       install_date, install_location, size_kb, first_seen_at, "
        "       last_seen_at "
        "FROM agent_apps WHERE agent_id = ? AND removed_at = '' "
        "ORDER BY name COLLATE NOCASE, version, app_key;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent application list");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_TRANSIENT);
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cmdb_agent_app_t *app;
    if (used == capacity) {
      size_t next = capacity == 0 ? 16 : capacity * 2;
      apps = cmdb_realloc(apps, next * sizeof(*apps));
      capacity = next;
    }
    app = &apps[used++];
    memset(app, 0, sizeof(*app));
    agent_str(app->app_key, sizeof(app->app_key),
              (const char *)sqlite3_column_text(stmt, 0));
    agent_str(app->name, sizeof(app->name),
              (const char *)sqlite3_column_text(stmt, 1));
    agent_str(app->version, sizeof(app->version),
              (const char *)sqlite3_column_text(stmt, 2));
    agent_str(app->publisher, sizeof(app->publisher),
              (const char *)sqlite3_column_text(stmt, 3));
    agent_str(app->source, sizeof(app->source),
              (const char *)sqlite3_column_text(stmt, 4));
    agent_str(app->architecture, sizeof(app->architecture),
              (const char *)sqlite3_column_text(stmt, 5));
    agent_str(app->install_date, sizeof(app->install_date),
              (const char *)sqlite3_column_text(stmt, 6));
    agent_str(app->install_location, sizeof(app->install_location),
              (const char *)sqlite3_column_text(stmt, 7));
    app->size_kb = sqlite3_column_int64(stmt, 8);
    agent_str(app->first_seen_at, sizeof(app->first_seen_at),
              (const char *)sqlite3_column_text(stmt, 9));
    agent_str(app->last_seen_at, sizeof(app->last_seen_at),
              (const char *)sqlite3_column_text(stmt, 10));
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    free(apps);
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot read agent applications");
    return -1;
  }
  *out = apps;
  *count = used;
  return 0;
}

int cmdb_agent_history(cmdb_db_t *db, const char *agent_id,
                       size_t report_limit, size_t event_limit,
                       cmdb_agent_report_t **reports, size_t *report_count,
                       cmdb_agent_event_t **events, size_t *event_count,
                       cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  cmdb_agent_report_t *report_items = NULL;
  cmdb_agent_event_t *event_items = NULL;
  size_t reports_used = 0;
  size_t reports_capacity = 0;
  size_t events_used = 0;
  size_t events_capacity = 0;
  int rc;

  if (db == NULL || !cmdb_uuid_valid(agent_id) || reports == NULL ||
      report_count == NULL || events == NULL || event_count == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid agent history arguments");
    return -1;
  }
  *reports = NULL;
  *report_count = 0;
  *events = NULL;
  *event_count = 0;
  if (report_limit == 0) {
    report_limit = 30;
  }
  if (report_limit > 365) {
    report_limit = 365;
  }
  if (event_limit == 0) {
    event_limit = 200;
  }
  if (event_limit > 2000) {
    event_limit = 2000;
  }
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT id, collected_at, received_at, app_count, added_count, "
        "       removed_count, updated_count, complete, collector_errors "
        "FROM agent_reports WHERE agent_id = ? "
        "ORDER BY received_at DESC, rowid DESC LIMIT ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent report history");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, (int)report_limit);
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cmdb_agent_report_t *item;
    if (reports_used == reports_capacity) {
      size_t next = reports_capacity == 0 ? 8 : reports_capacity * 2;
      report_items = cmdb_realloc(report_items, next * sizeof(*report_items));
      reports_capacity = next;
    }
    item = &report_items[reports_used++];
    memset(item, 0, sizeof(*item));
    agent_str(item->id, sizeof(item->id),
              (const char *)sqlite3_column_text(stmt, 0));
    agent_str(item->collected_at, sizeof(item->collected_at),
              (const char *)sqlite3_column_text(stmt, 1));
    agent_str(item->received_at, sizeof(item->received_at),
              (const char *)sqlite3_column_text(stmt, 2));
    item->app_count = (size_t)sqlite3_column_int64(stmt, 3);
    item->added_count = (size_t)sqlite3_column_int64(stmt, 4);
    item->removed_count = (size_t)sqlite3_column_int64(stmt, 5);
    item->updated_count = (size_t)sqlite3_column_int64(stmt, 6);
    item->complete = sqlite3_column_int(stmt, 7) != 0;
    agent_str(item->collector_errors, sizeof(item->collector_errors),
              (const char *)sqlite3_column_text(stmt, 8));
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    free(report_items);
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot read agent report history");
    return -1;
  }

  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT id, report_id, app_key, event_type, name, old_version, "
        "       new_version, detail, occurred_at "
        "FROM agent_app_events WHERE agent_id = ? "
        "ORDER BY occurred_at DESC, id DESC LIMIT ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    free(report_items);
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare application change history");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, agent_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, (int)event_limit);
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cmdb_agent_event_t *item;
    if (events_used == events_capacity) {
      size_t next = events_capacity == 0 ? 16 : events_capacity * 2;
      event_items = cmdb_realloc(event_items, next * sizeof(*event_items));
      events_capacity = next;
    }
    item = &event_items[events_used++];
    memset(item, 0, sizeof(*item));
    item->id[0] = '\0';
    {
      char id_buf[37];
      (void)snprintf(id_buf, sizeof(id_buf), "%lld",
                     sqlite3_column_int64(stmt, 0));
      agent_str(item->id, sizeof(item->id), id_buf);
    }
    agent_str(item->report_id, sizeof(item->report_id),
              (const char *)sqlite3_column_text(stmt, 1));
    agent_str(item->app_key, sizeof(item->app_key),
              (const char *)sqlite3_column_text(stmt, 2));
    agent_str(item->event_type, sizeof(item->event_type),
              (const char *)sqlite3_column_text(stmt, 3));
    agent_str(item->name, sizeof(item->name),
              (const char *)sqlite3_column_text(stmt, 4));
    agent_str(item->old_version, sizeof(item->old_version),
              (const char *)sqlite3_column_text(stmt, 5));
    agent_str(item->new_version, sizeof(item->new_version),
              (const char *)sqlite3_column_text(stmt, 6));
    agent_str(item->detail, sizeof(item->detail),
              (const char *)sqlite3_column_text(stmt, 7));
    agent_str(item->occurred_at, sizeof(item->occurred_at),
              (const char *)sqlite3_column_text(stmt, 8));
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    free(report_items);
    free(event_items);
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot read application change history");
    return -1;
  }
  *reports = report_items;
  *report_count = reports_used;
  *events = event_items;
  *event_count = events_used;
  return 0;
}

int cmdb_agent_link_ci(cmdb_db_t *db, const char *agent_id,
                       const char *ci_id, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char now[21];
  int rc;

  if (db == NULL || !cmdb_uuid_valid(agent_id) || ci_id == NULL ||
      (ci_id[0] != '\0' && !cmdb_uuid_valid(ci_id))) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid agent or CI ID");
    return -1;
  }
  if (ci_id[0] != '\0') {
    if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                           "SELECT 1 FROM cis WHERE id = ?;",
                           -1, &stmt, NULL) != SQLITE_OK) {
      cmdb_error_set(err, CMDB_ERR_DATABASE,
                     "Cannot prepare CI link validation");
      return -1;
    }
    sqlite3_bind_text(stmt, 1, ci_id, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_ROW) {
      cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "CI not found");
      return -1;
    }
  }
  cmdb_iso8601_now(now);
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE agents SET ci_id = ?, updated_at = ? WHERE id = ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent CI link");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, ci_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, agent_id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot link agent to CI");
    return -1;
  }
  if (cmdb_db_changes(db) == 0) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "Agent not found");
    return -1;
  }
  return 0;
}

int cmdb_agent_revoke(cmdb_db_t *db, const char *agent_id, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char now[21];
  int rc;

  if (db == NULL || !cmdb_uuid_valid(agent_id)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid agent ID");
    return -1;
  }
  cmdb_iso8601_now(now);
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE agents SET status = 'revoked', token_hash = '', "
        " token_prefix = '', updated_at = ? WHERE id = ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent revoke");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, agent_id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot revoke agent");
    return -1;
  }
  if (cmdb_db_changes(db) == 0) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "Agent not found");
    return -1;
  }
  return 0;
}

int cmdb_agent_prune(cmdb_db_t *db, int retention_days, int max_reports,
                     size_t *deleted, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char **ids = NULL;
  size_t count = 0;
  size_t capacity = 0;
  size_t i;
  int rc;

  if (db == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid database");
    return -1;
  }
  if (deleted != NULL) {
    *deleted = 0;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db), "SELECT id FROM agents;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare agent retention scan");
    return -1;
  }
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (count == capacity) {
      size_t next = capacity == 0 ? 16 : capacity * 2;
      ids = cmdb_realloc(ids, next * sizeof(*ids));
      capacity = next;
    }
    ids[count++] = cmdb_strdup((const char *)sqlite3_column_text(stmt, 0));
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    for (i = 0; i < count; i++) {
      free(ids[i]);
    }
    free(ids);
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot read agents for retention");
    return -1;
  }
  for (i = 0; i < count; i++) {
    if (agent_prune_one(db, ids[i], retention_days, max_reports, deleted,
                        err) != 0) {
      for (size_t j = 0; j < count; j++) {
        free(ids[j]);
      }
      free(ids);
      return -1;
    }
    free(ids[i]);
  }
  free(ids);
  return 0;
}

void cmdb_agent_list_free(cmdb_agent_t *agents) {
  free(agents);
}

void cmdb_agent_apps_free(cmdb_agent_app_t *apps) {
  free(apps);
}

void cmdb_agent_history_free(cmdb_agent_report_t *reports,
                             cmdb_agent_event_t *events) {
  free(reports);
  free(events);
}
