#include "cmdb/scan.h"

#include "cmdb/util.h"

#include <stdlib.h>
#include <string.h>

static int scan_store_is_uuid(const char *value) {
  return value != NULL && cmdb_uuid_valid(value);
}

static int scan_store_begin_job(cmdb_db_t *db, const char *target,
                                const char *ports, int service_detection,
                                int os_detection, char job_id[37],
                                cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char now[21];
  char id[37];
  int rc;

  if (cmdb_random_uuid(id) != 0) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot generate scan job ID");
    return -1;
  }
  cmdb_iso8601_now(now);
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "INSERT INTO scan_jobs"
        "(id, target, ports, method, command, service_detection, "
        " os_detection, status, started_at, finished_at, host_count, "
        " truncated, error, created_at) "
        "VALUES(?, ?, ?, 'nmap', '', ?, ?, 'running', ?, '', 0, 0, '', ?);",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare scan job");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, target, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, ports, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 4, service_detection);
  sqlite3_bind_int(stmt, 5, os_detection);
  sqlite3_bind_text(stmt, 6, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, now, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot create scan job");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
    return -1;
  }
  (void)cmdb_str_copy(job_id, 37, id);
  return 0;
}

int cmdb_scan_store_begin(cmdb_db_t *db, const char *target, const char *ports,
                          int service_detection, int os_detection,
                          char job_id[37], cmdb_error_t *err) {
  if (db == NULL || target == NULL || ports == NULL || job_id == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid scan job arguments");
    return -1;
  }
  job_id[0] = '\0';
  return scan_store_begin_job(db, target, ports, service_detection,
                              os_detection, job_id, err);
}

static int scan_store_insert_asset(cmdb_db_t *db, const char *job_id,
                                   const char *created_at,
                                   cmdb_scan_host_t *host,
                                   cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char id[37];
  int rc;

  if (cmdb_random_uuid(id) != 0) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot generate discovered asset ID");
    return -1;
  }
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "INSERT INTO discovered_assets"
        "(id, scan_job_id, ip_address, hostname, mac_address, vendor, "
        " os_guess, services, status, open_ports, port_count, "
        " ports_truncated, review_status, matched_ci_id, created_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'pending', '', ?);",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare discovered asset");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, job_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, host->ip_address, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, host->hostname, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, host->mac_address, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, host->vendor, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, host->os_guess, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, host->services, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 9, host->status, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 10, host->open_ports, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 11, (int)host->port_count);
  sqlite3_bind_int(stmt, 12, host->ports_truncated);
  sqlite3_bind_text(stmt, 13, created_at, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot store discovered asset");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
    return -1;
  }
  (void)cmdb_str_copy(host->asset_id, sizeof(host->asset_id), id);
  (void)cmdb_str_copy(host->review_status, sizeof(host->review_status),
                      "pending");
  return 0;
}

static cmdb_scan_match_t *scan_match_add(cmdb_scan_host_t *host,
                                          const char *ci_id,
                                          const char *ci_name,
                                          const char *type_name,
                                          const char *reason, int score) {
  size_t i;

  for (i = 0; i < host->match_count; i++) {
    if (strcmp(host->matches[i].ci_id, ci_id) == 0) {
      if (strstr(host->matches[i].reasons, reason) == NULL) {
        size_t used = strlen(host->matches[i].reasons);
        if (used + strlen(reason) + 3 <
            sizeof(host->matches[i].reasons)) {
          if (used > 0) {
            host->matches[i].reasons[used++] = ',';
            host->matches[i].reasons[used++] = ' ';
          }
          (void)cmdb_str_copy(host->matches[i].reasons + used,
                              sizeof(host->matches[i].reasons) - used,
                              reason);
        }
      }
      if (score > host->matches[i].score) {
        host->matches[i].score = score;
      }
      return &host->matches[i];
    }
  }
  if (host->match_count >= 5) {
    return NULL;
  }
  host->matches = cmdb_realloc(
    host->matches, (host->match_count + 1) * sizeof(*host->matches));
  memset(&host->matches[host->match_count], 0,
         sizeof(host->matches[host->match_count]));
  (void)cmdb_str_copy(host->matches[host->match_count].ci_id,
                      sizeof(host->matches[host->match_count].ci_id), ci_id);
  (void)cmdb_str_copy(host->matches[host->match_count].ci_name,
                      sizeof(host->matches[host->match_count].ci_name), ci_name);
  (void)cmdb_str_copy(host->matches[host->match_count].type_name,
                      sizeof(host->matches[host->match_count].type_name),
                      type_name);
  (void)cmdb_str_copy(host->matches[host->match_count].reasons,
                      sizeof(host->matches[host->match_count].reasons),
                      reason);
  host->matches[host->match_count].score = score;
  host->match_count++;
  return &host->matches[host->match_count - 1];
}

static int scan_collect_matches(cmdb_db_t *db, const char *sql,
                                const char *first, const char *second,
                                cmdb_scan_host_t *host,
                                const char *reason, int score,
                                cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_DONE;

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db), sql, -1, &stmt, NULL) !=
      SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare scan matching");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, first != NULL ? first : "", -1, SQLITE_TRANSIENT);
  if (second != NULL) {
    sqlite3_bind_text(stmt, 2, second, -1, SQLITE_TRANSIENT);
  }
  while (host->match_count < 5 && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const char *id = (const char *)sqlite3_column_text(stmt, 0);
    const char *name = (const char *)sqlite3_column_text(stmt, 1);
    const char *type = (const char *)sqlite3_column_text(stmt, 2);
    (void)scan_match_add(host, id != NULL ? id : "",
                         name != NULL ? name : "",
                         type != NULL ? type : "", reason, score);
  }
  if (rc != SQLITE_ROW && rc != SQLITE_DONE && host->match_count < 5) {
    sqlite3_finalize(stmt);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read scan matches");
    return -1;
  }
  sqlite3_finalize(stmt);
  return 0;
}

int cmdb_scan_match_assets(cmdb_db_t *db, cmdb_scan_result_t *result,
                           cmdb_error_t *err) {
  size_t i;
  static const char ip_sql[] =
    "SELECT c.id, c.name, t.name FROM cis c "
    "JOIN ci_types t ON t.id = c.type_id "
    "JOIN ci_attributes a ON a.ci_id = c.id "
    "WHERE a.field_key IN ('ip_address','management_ip') AND a.value = ?;";
  static const char mac_sql[] =
    "SELECT c.id, c.name, t.name FROM cis c "
    "JOIN ci_types t ON t.id = c.type_id "
    "JOIN ci_attributes a ON a.ci_id = c.id "
    "WHERE a.field_key = 'mac_address' "
    "AND lower(a.value) = lower(?);";
  static const char name_sql[] =
    "SELECT c.id, c.name, t.name FROM cis c "
    "JOIN ci_types t ON t.id = c.type_id "
    "WHERE c.name = ? COLLATE NOCASE;";
  static const char hostname_sql[] =
    "SELECT c.id, c.name, t.name FROM cis c "
    "JOIN ci_types t ON t.id = c.type_id "
    "JOIN ci_attributes a ON a.ci_id = c.id "
    "WHERE a.field_key = 'hostname' AND a.value = ? COLLATE NOCASE;";

  if (db == NULL || result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid scan matching arguments");
    return -1;
  }
  for (i = 0; i < result->host_count; i++) {
    cmdb_scan_host_t *host = &result->hosts[i];
    free(host->matches);
    host->matches = NULL;
    host->match_count = 0;
    if (host->ip_address[0] != '\0' &&
        scan_collect_matches(db, ip_sql, host->ip_address, NULL, host,
                             "IP address", 100, err) != 0) {
      return -1;
    }
    if (host->mac_address[0] != '\0' &&
        scan_collect_matches(db, mac_sql, host->mac_address, NULL, host,
                             "MAC address", 90, err) != 0) {
      return -1;
    }
    if (host->hostname[0] != '\0') {
      if (scan_collect_matches(db, name_sql, host->hostname, NULL, host,
                               "CI name", 80, err) != 0 ||
          scan_collect_matches(db, hostname_sql, host->hostname, NULL, host,
                               "hostname", 70, err) != 0) {
        return -1;
      }
    }
  }
  return 0;
}

int cmdb_scan_store_finish(cmdb_db_t *db, cmdb_scan_result_t *result,
                           const char *status, const char *error,
                           cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char now[21];
  char created_at[21];
  int rc;
  size_t i;

  if (db == NULL || result == NULL || result->job_id[0] == '\0' ||
      status == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid scan completion arguments");
    return -1;
  }
  cmdb_iso8601_now(created_at);
  if (result->finished_at[0] == '\0') {
    (void)cmdb_str_copy(result->finished_at, sizeof(result->finished_at),
                        created_at);
  }
  now[0] = '\0';
  (void)cmdb_str_copy(now, sizeof(now), result->finished_at);
  if (cmdb_db_savepoint_begin(db, "scan_finish", err) != 0) {
    return -1;
  }
  if (strcmp(status, "completed") == 0) {
    if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                           "DELETE FROM discovered_assets "
                           "WHERE scan_job_id = ?;",
                           -1, &stmt, NULL) != SQLITE_OK) {
      cmdb_error_set(err, CMDB_ERR_DATABASE,
                     "Cannot clear staged scan results");
      goto fail;
    }
    sqlite3_bind_text(stmt, 1, result->job_id, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE,
                     "Cannot clear staged scan results");
      goto fail;
    }
    for (i = 0; i < result->host_count; i++) {
      if (scan_store_insert_asset(db, result->job_id, created_at,
                                  &result->hosts[i], err) != 0) {
        goto fail;
      }
    }
  }
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE scan_jobs SET command = ?, service_detection = ?, "
        " os_detection = ?, status = ?, finished_at = ?, host_count = ?, "
        " truncated = ?, error = ?, ports = ? WHERE id = ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare scan completion");
    goto fail;
  }
  sqlite3_bind_text(stmt, 1, result->command, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, result->service_detection);
  sqlite3_bind_int(stmt, 3, result->os_detection);
  sqlite3_bind_text(stmt, 4, status, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 6, (int)result->host_count);
  sqlite3_bind_int(stmt, 7, result->truncated);
  sqlite3_bind_text(stmt, 8, error != NULL ? error : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 9, result->ports, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 10, result->job_id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot complete scan job");
    goto fail;
  }
  if (cmdb_db_savepoint_release(db, "scan_finish", err) != 0) {
    return -1;
  }
  if (strcmp(status, "completed") == 0) {
    return cmdb_scan_match_assets(db, result, err);
  }
  return 0;

fail:
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  (void)cmdb_db_savepoint_rollback(db, "scan_finish", NULL);
  return -1;
}

static int scan_store_fill_job(sqlite3_stmt *stmt, cmdb_scan_result_t *result) {
  const char *text;

  memset(result, 0, sizeof(*result));
  text = (const char *)sqlite3_column_text(stmt, 0);
  if (cmdb_str_copy(result->job_id, sizeof(result->job_id), text) != 0) {
    return -1;
  }
  text = (const char *)sqlite3_column_text(stmt, 1);
  if (cmdb_str_copy(result->target, sizeof(result->target), text) != 0) {
    return -1;
  }
  text = (const char *)sqlite3_column_text(stmt, 2);
  if (cmdb_str_copy(result->ports, sizeof(result->ports), text) != 0) {
    return -1;
  }
  text = (const char *)sqlite3_column_text(stmt, 3);
  if (cmdb_str_copy(result->method, sizeof(result->method), text) != 0) {
    return -1;
  }
  text = (const char *)sqlite3_column_text(stmt, 4);
  if (cmdb_str_copy(result->command, sizeof(result->command), text) != 0) {
    return -1;
  }
  result->service_detection = sqlite3_column_int(stmt, 5);
  result->os_detection = sqlite3_column_int(stmt, 6);
  text = (const char *)sqlite3_column_text(stmt, 7);
  if (cmdb_str_copy(result->status, sizeof(result->status), text) != 0) {
    return -1;
  }
  text = (const char *)sqlite3_column_text(stmt, 8);
  if (cmdb_str_copy(result->started_at, sizeof(result->started_at),
                    text) != 0) {
    return -1;
  }
  text = (const char *)sqlite3_column_text(stmt, 9);
  if (cmdb_str_copy(result->finished_at, sizeof(result->finished_at),
                    text) != 0) {
    return -1;
  }
  result->host_count = (size_t)sqlite3_column_int64(stmt, 10);
  result->truncated = sqlite3_column_int(stmt, 11);
  text = (const char *)sqlite3_column_text(stmt, 12);
  if (cmdb_str_copy(result->error, sizeof(result->error), text) != 0) {
    return -1;
  }
  return 0;
}

static int scan_store_load_assets(cmdb_db_t *db, cmdb_scan_result_t *result,
                                  cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  size_t capacity = 0;
  size_t index = 0;
  int rc;

  result->hosts = NULL;
  result->host_count = 0;
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT id, ip_address, hostname, mac_address, vendor, os_guess, "
        " services, status, open_ports, port_count, ports_truncated, "
        " review_status, matched_ci_id FROM discovered_assets "
        "WHERE scan_job_id = ? ORDER BY ip_address;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read staged scan assets");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, result->job_id, -1, SQLITE_TRANSIENT);
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cmdb_scan_host_t *host;
    const char *text;
    if (index >= CMDB_SCAN_MAX_HOSTS) {
      result->truncated = 1;
      continue;
    }
    if (index == capacity) {
      capacity = capacity == 0 ? 16 : capacity * 2;
      if (capacity > CMDB_SCAN_MAX_HOSTS) {
        capacity = CMDB_SCAN_MAX_HOSTS;
      }
      result->hosts = cmdb_realloc(
        result->hosts, capacity * sizeof(*result->hosts));
    }
    host = &result->hosts[index];
    memset(host, 0, sizeof(*host));
    text = (const char *)sqlite3_column_text(stmt, 0);
    (void)cmdb_str_copy(host->asset_id, sizeof(host->asset_id), text);
    text = (const char *)sqlite3_column_text(stmt, 1);
    (void)cmdb_str_copy(host->ip_address, sizeof(host->ip_address), text);
    text = (const char *)sqlite3_column_text(stmt, 2);
    (void)cmdb_str_copy(host->hostname, sizeof(host->hostname), text);
    text = (const char *)sqlite3_column_text(stmt, 3);
    (void)cmdb_str_copy(host->mac_address, sizeof(host->mac_address), text);
    text = (const char *)sqlite3_column_text(stmt, 4);
    (void)cmdb_str_copy(host->vendor, sizeof(host->vendor), text);
    text = (const char *)sqlite3_column_text(stmt, 5);
    (void)cmdb_str_copy(host->os_guess, sizeof(host->os_guess), text);
    text = (const char *)sqlite3_column_text(stmt, 6);
    (void)cmdb_str_copy(host->services, sizeof(host->services), text);
    text = (const char *)sqlite3_column_text(stmt, 7);
    (void)cmdb_str_copy(host->status, sizeof(host->status), text);
    text = (const char *)sqlite3_column_text(stmt, 8);
    (void)cmdb_str_copy(host->open_ports, sizeof(host->open_ports), text);
    host->port_count = (size_t)sqlite3_column_int64(stmt, 9);
    host->ports_truncated = sqlite3_column_int(stmt, 10);
    text = (const char *)sqlite3_column_text(stmt, 11);
    (void)cmdb_str_copy(host->review_status, sizeof(host->review_status),
                        text);
    index++;
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_scan_result_free(result);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read staged scan assets");
    return -1;
  }
  result->host_count = index;
  return cmdb_scan_match_assets(db, result, err);
}

int cmdb_scan_store_load(cmdb_db_t *db, const char *job_id,
                         cmdb_scan_result_t *result, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || job_id == NULL || result == NULL ||
      !scan_store_is_uuid(job_id)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid scan job ID");
    return -1;
  }
  memset(result, 0, sizeof(*result));
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT id, target, ports, method, command, service_detection, "
        " os_detection, status, started_at, finished_at, host_count, "
        " truncated, error FROM scan_jobs WHERE id = ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read scan job");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, job_id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "Scan job not found");
    } else {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read scan job");
    }
    return -1;
  }
  if (scan_store_fill_job(stmt, result) != 0) {
    sqlite3_finalize(stmt);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Scan job value is too long");
    return -1;
  }
  sqlite3_finalize(stmt);
  if (scan_store_load_assets(db, result, err) != 0) {
    return -1;
  }
  return 0;
}

int cmdb_scan_store_latest(cmdb_db_t *db, cmdb_scan_result_t *result,
                           cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  char id[37];
  int rc;

  if (db == NULL || result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid scan job lookup");
    return -1;
  }
  memset(result, 0, sizeof(*result));
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT id FROM scan_jobs "
                         "ORDER BY created_at DESC LIMIT 1;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read scan jobs");
    return -1;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "No staged scans exist");
    } else {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read scan jobs");
    }
    return -1;
  }
  (void)cmdb_str_copy(id, sizeof(id),
                      (const char *)sqlite3_column_text(stmt, 0));
  sqlite3_finalize(stmt);
  return cmdb_scan_store_load(db, id, result, err);
}

int cmdb_scan_jobs_list(cmdb_db_t *db, cmdb_scan_job_summary_t **out,
                        size_t *count, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  cmdb_scan_job_summary_t *items;
  size_t index = 0;
  int rc;

  if (db == NULL || out == NULL || count == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid job list");
    return -1;
  }
  *out = NULL;
  *count = 0;
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT j.id, j.target, j.status, j.started_at, j.finished_at, "
        " j.host_count, "
        " (SELECT count(*) FROM discovered_assets a "
        "  WHERE a.scan_job_id = j.id AND a.review_status = 'pending'), "
        " j.truncated, j.service_detection, j.os_detection "
        "FROM scan_jobs j ORDER BY j.created_at DESC LIMIT 50;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list scan jobs");
    return -1;
  }
  items = cmdb_calloc(50, sizeof(*items));
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW && index < 50) {
    const char *text;
    text = (const char *)sqlite3_column_text(stmt, 0);
    (void)cmdb_str_copy(items[index].id, sizeof(items[index].id), text);
    text = (const char *)sqlite3_column_text(stmt, 1);
    (void)cmdb_str_copy(items[index].target, sizeof(items[index].target), text);
    text = (const char *)sqlite3_column_text(stmt, 2);
    (void)cmdb_str_copy(items[index].status, sizeof(items[index].status), text);
    text = (const char *)sqlite3_column_text(stmt, 3);
    (void)cmdb_str_copy(items[index].started_at,
                        sizeof(items[index].started_at), text);
    text = (const char *)sqlite3_column_text(stmt, 4);
    (void)cmdb_str_copy(items[index].finished_at,
                        sizeof(items[index].finished_at), text);
    items[index].host_count = (size_t)sqlite3_column_int64(stmt, 5);
    items[index].pending_count = (size_t)sqlite3_column_int64(stmt, 6);
    items[index].truncated = sqlite3_column_int(stmt, 7);
    items[index].service_detection = sqlite3_column_int(stmt, 8);
    items[index].os_detection = sqlite3_column_int(stmt, 9);
    index++;
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
    free(items);
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list scan jobs");
    return -1;
  }
  *out = items;
  *count = index;
  return 0;
}

int cmdb_scan_jobs_delete(cmdb_db_t *db, const char *job_id,
                          cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || job_id == NULL || !scan_store_is_uuid(job_id)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid scan job ID");
    return -1;
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "DELETE FROM scan_jobs WHERE id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot delete scan job");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, job_id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot delete scan job");
    return -1;
  }
  return 0;
}

int cmdb_scan_jobs_recover(cmdb_db_t *db, cmdb_error_t *err) {
  char now[21];
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid database");
    return -1;
  }
  cmdb_iso8601_now(now);
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE scan_jobs SET status = 'interrupted', finished_at = ?, "
        " error = 'The application closed before the scan finished' "
        "WHERE status = 'running';",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot recover scan jobs");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, now, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot recover scan jobs");
    return -1;
  }
  return 0;
}

int cmdb_scan_asset_set_review(cmdb_db_t *db, const char *asset_id,
                               const char *review_status,
                               const char *matched_ci_id,
                               cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || asset_id == NULL || !scan_store_is_uuid(asset_id) ||
      review_status == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid discovered asset");
    return -1;
  }
  if (strcmp(review_status, "pending") != 0 &&
      strcmp(review_status, "merged") != 0 &&
      strcmp(review_status, "created") != 0 &&
      strcmp(review_status, "ignored") != 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Invalid review status");
    return -1;
  }
  if (matched_ci_id != NULL && matched_ci_id[0] != '\0' &&
      !scan_store_is_uuid(matched_ci_id)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Invalid matched CI ID");
    return -1;
  }
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE discovered_assets SET review_status = ?, matched_ci_id = ? "
        "WHERE id = ?;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot update discovered asset");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, review_status, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, matched_ci_id != NULL ? matched_ci_id : "",
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, asset_id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot update discovered asset");
    return -1;
  }
  return 0;
}

int cmdb_scan_job_pending(cmdb_db_t *db, const char *job_id,
                          size_t *pending, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || job_id == NULL || pending == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid scan pending count");
    return -1;
  }
  *pending = 0;
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT count(*) FROM discovered_assets "
        "WHERE scan_job_id = ? AND review_status = 'pending';",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot count staged assets");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, job_id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *pending = (size_t)sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_ROW) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot count staged assets");
    return -1;
  }
  return 0;
}
