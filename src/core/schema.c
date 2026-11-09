#include "cmdb/schema.h"

#include "cmdb/crypto.h"
#include "cmdb/types.h"
#include "cmdb/util.h"

#include <stdio.h>
#include <string.h>

#ifndef CMDB_VERSION
#define CMDB_VERSION "0.0.0"
#endif

static const char schema_sql[] =
  "CREATE TABLE IF NOT EXISTS app_meta (\n"
  "  key TEXT PRIMARY KEY NOT NULL,\n"
  "  value TEXT NOT NULL\n"
  ");\n"
  "CREATE TABLE IF NOT EXISTS ci_types (\n"
  "  id TEXT PRIMARY KEY NOT NULL,\n"
  "  name TEXT NOT NULL COLLATE NOCASE UNIQUE,\n"
  "  description TEXT NOT NULL DEFAULT '',\n"
  "  icon TEXT NOT NULL DEFAULT '',\n"
  "  color TEXT NOT NULL DEFAULT '',\n"
  "  created_at TEXT NOT NULL,\n"
  "  updated_at TEXT NOT NULL\n"
  ");\n"
  "CREATE TABLE IF NOT EXISTS ci_type_fields (\n"
  "  id TEXT PRIMARY KEY NOT NULL,\n"
  "  type_id TEXT NOT NULL REFERENCES ci_types(id) ON DELETE CASCADE,\n"
  "  field_key TEXT NOT NULL COLLATE NOCASE,\n"
  "  label TEXT NOT NULL,\n"
  "  field_type TEXT NOT NULL CHECK (field_type IN "
  "('text','number','date','boolean','url','select')),\n"
  "  required INTEGER NOT NULL DEFAULT 0 CHECK (required IN (0,1)),\n"
  "  options_json TEXT NOT NULL DEFAULT '[]',\n"
  "  sort_order INTEGER NOT NULL DEFAULT 0,\n"
  "  UNIQUE(type_id, field_key)\n"
  ");\n"
  "CREATE TABLE IF NOT EXISTS cis (\n"
  "  id TEXT PRIMARY KEY NOT NULL,\n"
  "  name TEXT NOT NULL,\n"
  "  type_id TEXT NOT NULL REFERENCES ci_types(id) ON DELETE RESTRICT,\n"
  "  description TEXT NOT NULL DEFAULT '',\n"
  "  environment TEXT NOT NULL DEFAULT '',\n"
  "  status TEXT NOT NULL DEFAULT '',\n"
  "  business_service TEXT NOT NULL DEFAULT '',\n"
  "  technical_owner TEXT NOT NULL DEFAULT '',\n"
  "  support_team TEXT NOT NULL DEFAULT '',\n"
  "  criticality TEXT NOT NULL DEFAULT '',\n"
  "  hosting_location TEXT NOT NULL DEFAULT '',\n"
  "  external_id TEXT,\n"
  "  created_at TEXT NOT NULL,\n"
  "  updated_at TEXT NOT NULL\n"
  ");\n"
  "CREATE UNIQUE INDEX IF NOT EXISTS idx_cis_external_id\n"
  "  ON cis(external_id)\n"
  "  WHERE external_id IS NOT NULL AND external_id <> '';\n"
  "CREATE INDEX IF NOT EXISTS idx_cis_name ON cis(name COLLATE NOCASE);\n"
  "CREATE INDEX IF NOT EXISTS idx_cis_type ON cis(type_id);\n"
  "CREATE TABLE IF NOT EXISTS ci_attributes (\n"
  "  ci_id TEXT NOT NULL REFERENCES cis(id) ON DELETE CASCADE,\n"
  "  field_key TEXT NOT NULL COLLATE NOCASE,\n"
  "  value TEXT NOT NULL DEFAULT '',\n"
  "  PRIMARY KEY (ci_id, field_key)\n"
  ");\n"
  "CREATE TABLE IF NOT EXISTS relationship_types (\n"
  "  id TEXT PRIMARY KEY NOT NULL,\n"
  "  name TEXT NOT NULL COLLATE NOCASE UNIQUE,\n"
  "  inverse_name TEXT NOT NULL DEFAULT '',\n"
  "  description TEXT NOT NULL DEFAULT '',\n"
  "  color TEXT NOT NULL DEFAULT '',\n"
  "  created_at TEXT NOT NULL,\n"
  "  updated_at TEXT NOT NULL\n"
  ");\n"
  "CREATE TABLE IF NOT EXISTS relationships (\n"
  "  id TEXT PRIMARY KEY NOT NULL,\n"
  "  source_id TEXT NOT NULL REFERENCES cis(id) ON DELETE CASCADE,\n"
  "  target_id TEXT NOT NULL REFERENCES cis(id) ON DELETE CASCADE,\n"
  "  type_id TEXT NOT NULL REFERENCES relationship_types(id) ON DELETE RESTRICT,\n"
  "  description TEXT NOT NULL DEFAULT '',\n"
  "  created_at TEXT NOT NULL,\n"
  "  updated_at TEXT NOT NULL,\n"
  "  UNIQUE(source_id, target_id, type_id),\n"
  "  CHECK(source_id <> target_id)\n"
  ");\n"
  "CREATE INDEX IF NOT EXISTS idx_rel_source ON relationships(source_id);\n"
  "CREATE INDEX IF NOT EXISTS idx_rel_target ON relationships(target_id);\n"
  "CREATE TABLE IF NOT EXISTS diagrams (\n"
  "  id TEXT PRIMARY KEY NOT NULL,\n"
  "  name TEXT NOT NULL,\n"
  "  drawio_xml TEXT NOT NULL,\n"
  "  created_at TEXT NOT NULL,\n"
  "  updated_at TEXT NOT NULL\n"
  ");\n"
  "CREATE TABLE IF NOT EXISTS audit_log (\n"
  "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
  "  ts TEXT NOT NULL,\n"
  "  actor TEXT NOT NULL,\n"
  "  action TEXT NOT NULL,\n"
  "  entity_type TEXT NOT NULL,\n"
  "  entity_id TEXT NOT NULL DEFAULT '',\n"
  "  detail TEXT NOT NULL DEFAULT '',\n"
  "  prev_hash TEXT NOT NULL,\n"
  "  hash TEXT NOT NULL\n"
  ");\n"
  "CREATE INDEX IF NOT EXISTS idx_audit_ts ON audit_log(ts);\n";

typedef struct {
  const char *id;
  const char *name;
  const char *description;
  const char *icon;
  const char *color;
} seed_type_t;

typedef struct {
  const char *type_id;
  const char *key;
  const char *label;
  const char *field_type;
  int required;
  int sort_order;
} seed_field_t;

typedef struct {
  const char *id;
  const char *name;
  const char *inverse;
  const char *description;
  const char *color;
} seed_rel_type_t;

static const seed_type_t seed_types[] = {
  {"11111111-1111-4111-8111-111111111111", "Linux Server",
   "Linux host or virtual machine", "server", "#dae8fc"},
  {"22222222-2222-4222-8222-222222222222", "Windows Server",
   "Windows host or virtual machine", "server", "#dae8fc"},
  {"33333333-3333-4333-8333-333333333333", "Virtual Machine",
   "Virtualised compute instance", "vm", "#e1d5e7"},
  {"44444444-4444-4444-8444-444444444444", "Router",
   "Network router", "router", "#d5e8d4"},
  {"55555555-5555-4555-8555-555555555555", "Switch",
   "Network switch", "switch", "#d5e8d4"},
  {"66666666-6666-4666-8666-666666666666", "Firewall",
   "Network firewall", "firewall", "#f8cecc"},
  {"77777777-7777-4777-8777-777777777777", "Web Application",
   "Software application or service", "app", "#fff2cc"},
  {"88888888-8888-4888-8888-888888888888", "Database",
   "Database service or instance", "database", "#ffe6cc"},
  {"99999999-9999-4999-8999-999999999999", "Business Service",
   "Business-facing service", "service", "#e6d0de"},
  {"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "Cloud Service",
   "Cloud platform or managed service", "cloud", "#b0e3e6"},
  {"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", "Network",
   "Network segment or network service", "network", "#d5e8d4"},
  {"cccccccc-cccc-4ccc-8ccc-cccccccccccc", "Other",
   "Any other configuration item", "other", "#f5f5f5"}
};

static const seed_field_t seed_fields[] = {
  {"11111111-1111-4111-8111-111111111111", "ip_address", "IP Address", "text", 0, 10},
  {"11111111-1111-4111-8111-111111111111", "serial_number", "Serial Number", "text", 0, 20},
  {"11111111-1111-4111-8111-111111111111", "os_version", "OS Version", "text", 0, 30},
  {"11111111-1111-4111-8111-111111111111", "hostname", "Hostname", "text", 0, 40},
  {"11111111-1111-4111-8111-111111111111", "cpu_cores", "CPU Cores", "number", 0, 50},
  {"11111111-1111-4111-8111-111111111111", "memory_gb", "Memory (GB)", "number", 0, 60},
  {"22222222-2222-4222-8222-222222222222", "ip_address", "IP Address", "text", 0, 10},
  {"22222222-2222-4222-8222-222222222222", "serial_number", "Serial Number", "text", 0, 20},
  {"22222222-2222-4222-8222-222222222222", "os_version", "OS Version", "text", 0, 30},
  {"22222222-2222-4222-8222-222222222222", "hostname", "Hostname", "text", 0, 40},
  {"22222222-2222-4222-8222-222222222222", "cpu_cores", "CPU Cores", "number", 0, 50},
  {"22222222-2222-4222-8222-222222222222", "memory_gb", "Memory (GB)", "number", 0, 60},
  {"33333333-3333-4333-8333-333333333333", "hypervisor", "Hypervisor", "text", 0, 10},
  {"33333333-3333-4333-8333-333333333333", "vcpu", "vCPU", "number", 0, 20},
  {"33333333-3333-4333-8333-333333333333", "memory_gb", "Memory (GB)", "number", 0, 30},
  {"33333333-3333-4333-8333-333333333333", "disk_gb", "Disk (GB)", "number", 0, 40},
  {"33333333-3333-4333-8333-333333333333", "ip_address", "IP Address", "text", 0, 50},
  {"33333333-3333-4333-8333-333333333333", "serial_number", "Serial Number", "text", 0, 60},
  {"44444444-4444-4444-8444-444444444444", "management_ip", "Management IP", "text", 0, 10},
  {"44444444-4444-4444-8444-444444444444", "vendor", "Vendor", "text", 0, 20},
  {"44444444-4444-4444-8444-444444444444", "model", "Model", "text", 0, 30},
  {"44444444-4444-4444-8444-444444444444", "serial_number", "Serial Number", "text", 0, 40},
  {"44444444-4444-4444-8444-444444444444", "firmware", "Firmware", "text", 0, 50},
  {"55555555-5555-4555-8555-555555555555", "management_ip", "Management IP", "text", 0, 10},
  {"55555555-5555-4555-8555-555555555555", "vendor", "Vendor", "text", 0, 20},
  {"55555555-5555-4555-8555-555555555555", "model", "Model", "text", 0, 30},
  {"55555555-5555-4555-8555-555555555555", "serial_number", "Serial Number", "text", 0, 40},
  {"55555555-5555-4555-8555-555555555555", "port_count", "Port Count", "number", 0, 50},
  {"66666666-6666-4666-8666-666666666666", "management_ip", "Management IP", "text", 0, 10},
  {"66666666-6666-4666-8666-666666666666", "vendor", "Vendor", "text", 0, 20},
  {"66666666-6666-4666-8666-666666666666", "model", "Model", "text", 0, 30},
  {"66666666-6666-4666-8666-666666666666", "serial_number", "Serial Number", "text", 0, 40},
  {"66666666-6666-4666-8666-666666666666", "ha_role", "HA Role", "text", 0, 50},
  {"77777777-7777-4777-8777-777777777777", "url", "URL", "url", 0, 10},
  {"77777777-7777-4777-8777-777777777777", "version", "Version", "text", 0, 20},
  {"77777777-7777-4777-8777-777777777777", "vendor", "Vendor", "text", 0, 30},
  {"77777777-7777-4777-8777-777777777777", "technology", "Technology", "text", 0, 40},
  {"77777777-7777-4777-8777-777777777777", "authentication", "Authentication", "text", 0, 50},
  {"88888888-8888-4888-8888-888888888888", "engine", "Engine", "text", 0, 10},
  {"88888888-8888-4888-8888-888888888888", "version", "Version", "text", 0, 20},
  {"88888888-8888-4888-8888-888888888888", "port", "Port", "number", 0, 30},
  {"88888888-8888-4888-8888-888888888888", "host", "Host", "text", 0, 40},
  {"88888888-8888-4888-8888-888888888888", "instance_name", "Instance Name", "text", 0, 50},
  {"99999999-9999-4999-8999-999999999999", "service_owner", "Service Owner", "text", 0, 10},
  {"99999999-9999-4999-8999-999999999999", "sla", "SLA", "text", 0, 20},
  {"99999999-9999-4999-8999-999999999999", "availability_target", "Availability Target", "text", 0, 30},
  {"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "provider", "Provider", "text", 0, 10},
  {"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "region", "Region", "text", 0, 20},
  {"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "account_id", "Account ID", "text", 0, 30},
  {"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "service_tier", "Service Tier", "text", 0, 40},
  {"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", "cidr", "CIDR", "text", 0, 10},
  {"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", "vlan", "VLAN", "text", 0, 20},
  {"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", "gateway", "Gateway", "text", 0, 30},
  {"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", "site", "Site", "text", 0, 40}
};

static const seed_rel_type_t seed_rel_types[] = {
  {"d0000000-0000-4000-8000-000000000001", "runs_on", "hosts",
   "Application or service runs on a host", "#6c8ebf"},
  {"d0000000-0000-4000-8000-000000000002", "uses", "used_by",
   "Component uses another component", "#9673a6"},
  {"d0000000-0000-4000-8000-000000000003", "hosted_in", "hosts",
   "CI is hosted in a location or platform", "#82b366"},
  {"d0000000-0000-4000-8000-000000000004", "supports", "supported_by",
   "CI supports a business service", "#d79b00"},
  {"d0000000-0000-4000-8000-000000000005", "depends_on", "depended_on_by",
   "CI depends on another CI", "#b85450"},
  {"d0000000-0000-4000-8000-000000000006", "connects_to", "connects_to",
   "Network or physical connection", "#666666"},
  {"d0000000-0000-4000-8000-000000000007", "member_of", "contains",
   "CI is a member of a group or service", "#a680b8"}
};

int cmdb_schema_create(cmdb_db_t *db, cmdb_error_t *err) {
  return cmdb_db_exec(db, schema_sql, err);
}

int cmdb_schema_meta_set(cmdb_db_t *db, const char *key, const char *value,
                         cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || key == NULL || value == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid metadata key");
    return -1;
  }

  rc = sqlite3_prepare_v2(cmdb_db_sqlite(db),
                          "INSERT OR REPLACE INTO app_meta(key, value) "
                          "VALUES(?, ?);",
                          -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot write application metadata");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
    return -1;
  }

  sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot write application metadata");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
    return -1;
  }
  return 0;
}

int cmdb_schema_meta_get(cmdb_db_t *db, const char *key,
                         char *out, size_t out_size, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == NULL || key == NULL || out == NULL || out_size == 0) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid metadata key");
    return -1;
  }
  out[0] = '\0';

  rc = sqlite3_prepare_v2(cmdb_db_sqlite(db),
                          "SELECT value FROM app_meta WHERE key = ?;",
                          -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read application metadata");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
    return -1;
  }
  sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    const unsigned char *value = sqlite3_column_text(stmt, 0);
    if (cmdb_str_copy(out, out_size,
                      (const char *)(value != NULL ? value : (const unsigned char *)"")) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Metadata value is too long");
      return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "Application metadata is missing");
    return -1;
  }
  cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read application metadata");
  cmdb_error_set_detail(err, "%s", sqlite3_errmsg(cmdb_db_sqlite(db)));
  return -1;
}

int cmdb_schema_validate(cmdb_db_t *db, cmdb_error_t *err) {
  char value[64];
  int version = 0;

  if (cmdb_schema_meta_get(db, "schema_version", value, sizeof(value), err) != 0) {
    return -1;
  }
  if (cmdb_parse_int(value, 0, 1000000, &version) != 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Invalid schema version");
    return -1;
  }
  if (version != CMDB_SCHEMA_VERSION) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Unsupported database schema version");
    cmdb_error_set_detail(err, "found %d, expected %d",
                          version, CMDB_SCHEMA_VERSION);
    return -1;
  }
  return 0;
}

int cmdb_schema_seed(cmdb_db_t *db, cmdb_error_t *err) {
  sqlite3 *handle;
  sqlite3_stmt *type_stmt = NULL;
  sqlite3_stmt *field_stmt = NULL;
  sqlite3_stmt *rel_stmt = NULL;
  char now[21];
  char uuid[37];
  size_t i;

  if (db == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid database");
    return -1;
  }
  handle = cmdb_db_sqlite(db);
  cmdb_iso8601_now(now);

  if (cmdb_db_begin(db, err) != 0) {
    return -1;
  }

  if (sqlite3_prepare_v2(handle,
                         "INSERT OR IGNORE INTO ci_types"
                         "(id, name, description, icon, color, created_at, updated_at) "
                         "VALUES(?, ?, ?, ?, ?, ?, ?);",
                         -1, &type_stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare type seed");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(handle));
    goto fail;
  }

  for (i = 0; i < sizeof(seed_types) / sizeof(seed_types[0]); i++) {
    sqlite3_reset(type_stmt);
    sqlite3_clear_bindings(type_stmt);
    sqlite3_bind_text(type_stmt, 1, seed_types[i].id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(type_stmt, 2, seed_types[i].name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(type_stmt, 3, seed_types[i].description, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(type_stmt, 4, seed_types[i].icon, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(type_stmt, 5, seed_types[i].color, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(type_stmt, 6, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(type_stmt, 7, now, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(type_stmt) != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot seed CI types");
      cmdb_error_set_detail(err, "%s", sqlite3_errmsg(handle));
      goto fail;
    }
  }

  if (sqlite3_prepare_v2(handle,
                         "INSERT OR IGNORE INTO ci_type_fields"
                         "(id, type_id, field_key, label, field_type, required, "
                         "options_json, sort_order) "
                         "VALUES(?, ?, ?, ?, ?, ?, '[]', ?);",
                         -1, &field_stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare field seed");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(handle));
    goto fail;
  }

  for (i = 0; i < sizeof(seed_fields) / sizeof(seed_fields[0]); i++) {
    if (cmdb_random_uuid(uuid) != 0) {
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot generate UUID");
      goto fail;
    }
    sqlite3_reset(field_stmt);
    sqlite3_clear_bindings(field_stmt);
    sqlite3_bind_text(field_stmt, 1, uuid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(field_stmt, 2, seed_fields[i].type_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(field_stmt, 3, seed_fields[i].key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(field_stmt, 4, seed_fields[i].label, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(field_stmt, 5, seed_fields[i].field_type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(field_stmt, 6, seed_fields[i].required);
    sqlite3_bind_int(field_stmt, 7, seed_fields[i].sort_order);
    if (sqlite3_step(field_stmt) != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot seed CI type fields");
      cmdb_error_set_detail(err, "%s", sqlite3_errmsg(handle));
      goto fail;
    }
  }

  if (sqlite3_prepare_v2(handle,
                         "INSERT OR IGNORE INTO relationship_types"
                         "(id, name, inverse_name, description, color, "
                         "created_at, updated_at) "
                         "VALUES(?, ?, ?, ?, ?, ?, ?);",
                         -1, &rel_stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare relationship seed");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(handle));
    goto fail;
  }

  for (i = 0; i < sizeof(seed_rel_types) / sizeof(seed_rel_types[0]); i++) {
    sqlite3_reset(rel_stmt);
    sqlite3_clear_bindings(rel_stmt);
    sqlite3_bind_text(rel_stmt, 1, seed_rel_types[i].id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(rel_stmt, 2, seed_rel_types[i].name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(rel_stmt, 3, seed_rel_types[i].inverse, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(rel_stmt, 4, seed_rel_types[i].description, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(rel_stmt, 5, seed_rel_types[i].color, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(rel_stmt, 6, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(rel_stmt, 7, now, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(rel_stmt) != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot seed relationship types");
      cmdb_error_set_detail(err, "%s", sqlite3_errmsg(handle));
      goto fail;
    }
  }

  sqlite3_finalize(type_stmt);
  sqlite3_finalize(field_stmt);
  sqlite3_finalize(rel_stmt);
  type_stmt = NULL;
  field_stmt = NULL;
  rel_stmt = NULL;

  {
    char version[32];
    (void)snprintf(version, sizeof(version), "%d", CMDB_SCHEMA_VERSION);
    if (cmdb_schema_meta_set(db, "schema_version", version, err) != 0 ||
        cmdb_schema_meta_set(db, "created_at", now, err) != 0 ||
        cmdb_schema_meta_set(db, "app_version", CMDB_VERSION, err) != 0) {
      goto fail;
    }
  }

  if (cmdb_db_commit(db, err) != 0) {
    return -1;
  }
  return 0;

fail:
  if (type_stmt != NULL) {
    sqlite3_finalize(type_stmt);
  }
  if (field_stmt != NULL) {
    sqlite3_finalize(field_stmt);
  }
  if (rel_stmt != NULL) {
    sqlite3_finalize(rel_stmt);
  }
  (void)cmdb_db_rollback(db, NULL);
  return -1;
}
