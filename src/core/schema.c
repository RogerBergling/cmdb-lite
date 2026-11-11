#include "cmdb/schema.h"

#include "cmdb/crypto.h"
#include "cmdb/types.h"
#include "cmdb/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CMDB_VERSION
#define CMDB_VERSION "0.0.0"
#endif

static const char schema_sql[] =
  "CREATE TABLE IF NOT EXISTS app_meta (\n"
  "  key TEXT PRIMARY KEY NOT NULL,\n"
  "  value TEXT NOT NULL\n"
  ");\n"
  "CREATE TABLE IF NOT EXISTS ci_categories (\n"
  "  id TEXT PRIMARY KEY NOT NULL,\n"
  "  name TEXT NOT NULL COLLATE NOCASE UNIQUE,\n"
  "  description TEXT NOT NULL DEFAULT '',\n"
  "  color TEXT NOT NULL DEFAULT '',\n"
  "  sort_order INTEGER NOT NULL DEFAULT 0,\n"
  "  created_at TEXT NOT NULL,\n"
  "  updated_at TEXT NOT NULL\n"
  ");\n"
  "CREATE TABLE IF NOT EXISTS ci_types (\n"
  "  id TEXT PRIMARY KEY NOT NULL,\n"
  "  name TEXT NOT NULL COLLATE NOCASE UNIQUE,\n"
  "  description TEXT NOT NULL DEFAULT '',\n"
  "  icon TEXT NOT NULL DEFAULT '',\n"
  "  color TEXT NOT NULL DEFAULT '',\n"
  "  category TEXT NOT NULL DEFAULT '',\n"
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
  "  default_impact TEXT NOT NULL DEFAULT 'availability',\n"
  "  created_at TEXT NOT NULL,\n"
  "  updated_at TEXT NOT NULL\n"
  ");\n"
  "CREATE TABLE IF NOT EXISTS relationships (\n"
  "  id TEXT PRIMARY KEY NOT NULL,\n"
  "  source_id TEXT NOT NULL REFERENCES cis(id) ON DELETE CASCADE,\n"
  "  target_id TEXT NOT NULL REFERENCES cis(id) ON DELETE CASCADE,\n"
  "  type_id TEXT NOT NULL REFERENCES relationship_types(id) ON DELETE RESTRICT,\n"
  "  description TEXT NOT NULL DEFAULT '',\n"
  "  impact TEXT NOT NULL DEFAULT 'availability',\n"
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

static const char categories_schema_sql[] =
  "CREATE TABLE IF NOT EXISTS ci_categories (\n"
  "  id TEXT PRIMARY KEY NOT NULL,\n"
  "  name TEXT NOT NULL COLLATE NOCASE UNIQUE,\n"
  "  description TEXT NOT NULL DEFAULT '',\n"
  "  color TEXT NOT NULL DEFAULT '',\n"
  "  sort_order INTEGER NOT NULL DEFAULT 0,\n"
  "  created_at TEXT NOT NULL,\n"
  "  updated_at TEXT NOT NULL\n"
  ");\n";

static const char scan_schema_sql[] =  "CREATE TABLE IF NOT EXISTS scan_jobs (\n"
  "  id TEXT PRIMARY KEY NOT NULL,\n"
  "  target TEXT NOT NULL,\n"
  "  ports TEXT NOT NULL,\n"
  "  method TEXT NOT NULL DEFAULT 'nmap',\n"
  "  command TEXT NOT NULL DEFAULT '',\n"
  "  service_detection INTEGER NOT NULL DEFAULT 0,\n"
  "  os_detection INTEGER NOT NULL DEFAULT 0,\n"
  "  status TEXT NOT NULL DEFAULT 'running',\n"
  "  started_at TEXT NOT NULL,\n"
  "  finished_at TEXT NOT NULL DEFAULT '',\n"
  "  host_count INTEGER NOT NULL DEFAULT 0,\n"
  "  truncated INTEGER NOT NULL DEFAULT 0,\n"
  "  error TEXT NOT NULL DEFAULT '',\n"
  "  created_at TEXT NOT NULL\n"
  ");\n"
  "CREATE INDEX IF NOT EXISTS idx_scan_jobs_created ON scan_jobs(created_at);\n"
  "CREATE TABLE IF NOT EXISTS discovered_assets (\n"
  "  id TEXT PRIMARY KEY NOT NULL,\n"
  "  scan_job_id TEXT NOT NULL REFERENCES scan_jobs(id) ON DELETE CASCADE,\n"
  "  ip_address TEXT NOT NULL,\n"
  "  hostname TEXT NOT NULL DEFAULT '',\n"
  "  mac_address TEXT NOT NULL DEFAULT '',\n"
  "  vendor TEXT NOT NULL DEFAULT '',\n"
  "  os_guess TEXT NOT NULL DEFAULT '',\n"
  "  services TEXT NOT NULL DEFAULT '',\n"
  "  status TEXT NOT NULL DEFAULT '',\n"
  "  open_ports TEXT NOT NULL DEFAULT '',\n"
  "  port_count INTEGER NOT NULL DEFAULT 0,\n"
  "  ports_truncated INTEGER NOT NULL DEFAULT 0,\n"
  "  review_status TEXT NOT NULL DEFAULT 'pending',\n"
  "  matched_ci_id TEXT NOT NULL DEFAULT '',\n"
  "  created_at TEXT NOT NULL,\n"
  "  UNIQUE(scan_job_id, ip_address)\n"
  ");\n"
  "CREATE INDEX IF NOT EXISTS idx_discovered_job\n"
  "  ON discovered_assets(scan_job_id);\n"
  "CREATE INDEX IF NOT EXISTS idx_discovered_status\n"
  "  ON discovered_assets(review_status);\n";

typedef struct {
  const char *id;
  const char *name;
  const char *description;
  const char *color;
  int sort_order;
} seed_category_t;

typedef struct {
  const char *id;
  const char *name;
  const char *description;
  const char *icon;
  const char *color;
  const char *category;
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
  const char *default_impact;
} seed_rel_type_t;

static const seed_category_t seed_categories[] = {
  {"c1111111-1111-4111-8111-111111111111", "Compute",
   "Physical and virtual compute resources", "#dae8fc", 10},
  {"c2222222-2222-4222-8222-222222222222", "Network",
   "Network devices and segments", "#d5e8d4", 20},
  {"c3333333-3333-4333-8333-333333333333", "Applications",
   "Applications and databases", "#fff2cc", 30},
  {"c4444444-4444-4444-8444-444444444444", "Services",
   "Business and cloud services", "#e6d0de", 40},
  {"c5555555-5555-4555-8555-555555555555", "Other",
   "Uncategorised and miscellaneous types", "#f5f5f5", 50}
};

static const seed_type_t seed_types[] = {
  {"11111111-1111-4111-8111-111111111111", "Linux Server",
   "Linux host or virtual machine", "server", "#dae8fc", "Compute"},
  {"22222222-2222-4222-8222-222222222222", "Windows Server",
   "Windows host or virtual machine", "server", "#dae8fc", "Compute"},
  {"33333333-3333-4333-8333-333333333333", "Virtual Machine",
   "Virtualised compute instance", "vm", "#e1d5e7", "Compute"},
  {"44444444-4444-4444-8444-444444444444", "Router",
   "Network router", "router", "#d5e8d4", "Network"},
  {"55555555-5555-4555-8555-555555555555", "Switch",
   "Network switch", "switch", "#d5e8d4", "Network"},
  {"66666666-6666-4666-8666-666666666666", "Firewall",
   "Network firewall", "firewall", "#f8cecc", "Network"},
  {"77777777-7777-4777-8777-777777777777", "Web Application",
   "Software application or service", "app", "#fff2cc", "Applications"},
  {"88888888-8888-4888-8888-888888888888", "Database",
   "Database service or instance", "database", "#ffe6cc", "Applications"},
  {"99999999-9999-4999-8999-999999999999", "Business Service",
   "Business-facing service", "service", "#e6d0de", "Services"},
  {"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "Cloud Service",
   "Cloud platform or managed service", "cloud", "#b0e3e6", "Services"},
  {"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", "Network",
   "Network segment or network service", "network", "#d5e8d4", "Network"},
  {"cccccccc-cccc-4ccc-8ccc-cccccccccccc", "Other",
   "Any other configuration item", "other", "#f5f5f5", "Other"},
  {"dddddddd-dddd-4ddd-8ddd-dddddddddddd", "Service",
   "A technical service such as DNS, SMTP, or NTP, provided by one or more resources",
   "service", "#e6d0de", "Services"}
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
  {"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", "site", "Site", "text", 0, 40},
  {"dddddddd-dddd-4ddd-8ddd-dddddddddddd", "service_type", "Service Type",
   "select", 0, 10},
  {"dddddddd-dddd-4ddd-8ddd-dddddddddddd", "service_endpoint", "Endpoint",
   "text", 0, 20},
  {"dddddddd-dddd-4ddd-8ddd-dddddddddddd", "service_port", "Port",
   "number", 0, 30},
  {"dddddddd-dddd-4ddd-8ddd-dddddddddddd", "service_protocol", "Protocol",
   "select", 0, 40},
  {"dddddddd-dddd-4ddd-8ddd-dddddddddddd", "availability_mode",
   "Availability Mode", "select", 0, 50},
  {"dddddddd-dddd-4ddd-8ddd-dddddddddddd", "service_scope", "Scope",
   "text", 0, 60}
};

static const seed_rel_type_t seed_rel_types[] = {
  {"d0000000-0000-4000-8000-000000000001", "runs_on", "hosts",
   "Application or service runs on a host", "#6c8ebf", "availability"},
  {"d0000000-0000-4000-8000-000000000002", "uses", "used_by",
   "Component uses another component", "#9673a6", "availability"},
  {"d0000000-0000-4000-8000-000000000003", "hosted_in", "hosts",
   "CI is hosted in a location or platform", "#82b366", "availability"},
  {"d0000000-0000-4000-8000-000000000004", "supports", "supported_by",
   "CI supports a business service", "#d79b00", "informational"},
  {"d0000000-0000-4000-8000-000000000005", "depends_on", "depended_on_by",
   "CI depends on another CI", "#b85450", "availability"},
  {"d0000000-0000-4000-8000-000000000006", "connects_to", "connects_to",
   "Network or physical connection", "#666666", "informational"},
  {"d0000000-0000-4000-8000-000000000007", "member_of", "contains",
   "CI is a member of a group or service", "#a680b8", "informational"},
  {"d0000000-0000-4000-8000-000000000008", "manages", "managed_by",
   "CI manages another CI; management plane only", "#6c8ebf", "management"},
  {"d0000000-0000-4000-8000-000000000009", "monitors", "monitored_by",
   "CI monitors another CI; observability only", "#9673a6", "monitoring"},
  {"d0000000-0000-4000-8000-00000000000a", "provided_by", "provides",
   "Service is provided by a resource", "#82b366", "availability"}
};

static const char *seed_field_options(const char *type_id, const char *key) {
  if (type_id != NULL && key != NULL &&
      strcmp(type_id, "dddddddd-dddd-4ddd-8ddd-dddddddddddd") == 0) {
    if (strcmp(key, "service_type") == 0) {
      return "[\"DNS\",\"SMTP\",\"NTP\",\"DHCP\",\"LDAP/AD\",\"vCenter\","
             "\"Monitoring\",\"Backup\",\"Other\"]";
    }
    if (strcmp(key, "service_protocol") == 0) {
      return "[\"TCP\",\"UDP\",\"TCP and UDP\",\"Other\"]";
    }
    if (strcmp(key, "availability_mode") == 0) {
      return "[\"Single\",\"Redundant\",\"Active/passive\",\"Active/active\"]";
    }
  }
  return "[]";
}

int cmdb_schema_create(cmdb_db_t *db, cmdb_error_t *err) {
  if (cmdb_db_exec(db, schema_sql, err) != 0) {
    return -1;
  }
  return cmdb_db_exec(db, scan_schema_sql, err);
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

static int schema_migrate_categories(cmdb_db_t *db, const char *now,
                                     cmdb_error_t *err) {
  sqlite3_stmt *insert = NULL;
  sqlite3_stmt *select = NULL;
  sqlite3_stmt *find = NULL;
  sqlite3_stmt *update = NULL;
  char **names = NULL;
  size_t name_count = 0;
  size_t capacity = 0;
  size_t i;
  int rc;

  if (cmdb_db_exec(db, categories_schema_sql, err) != 0) {
    return -1;
  }
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "INSERT INTO ci_categories"
        "(id, name, description, color, sort_order, created_at, updated_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?);",
        -1, &insert, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare category migration");
    return -1;
  }
  for (i = 0; i < sizeof(seed_categories) / sizeof(seed_categories[0]); i++) {
    sqlite3_reset(insert);
    sqlite3_clear_bindings(insert);
    sqlite3_bind_text(insert, 1, seed_categories[i].id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert, 2, seed_categories[i].name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert, 3, seed_categories[i].description, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(insert, 4, seed_categories[i].color, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(insert, 5, seed_categories[i].sort_order);
    sqlite3_bind_text(insert, 6, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert, 7, now, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(insert);
    if (rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT) {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot seed categories");
      goto fail;
    }
  }

  /*
   * Collect the legacy names before updating ci_types. Updating the table
   * while iterating the DISTINCT cursor can make SQLite return the newly
   * written category IDs as if they were legacy names.
   */
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT DISTINCT category COLLATE NOCASE FROM ci_types "
        "WHERE category <> '';",
        -1, &select, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare category migration");
    goto fail;
  }
  while ((rc = sqlite3_step(select)) == SQLITE_ROW) {
    const char *name = (const char *)sqlite3_column_text(select, 0);
    if (name == NULL || name[0] == '\0') {
      continue;
    }
    if (name_count == capacity) {
      size_t next = capacity == 0 ? 8 : capacity * 2;
      names = cmdb_realloc(names, next * sizeof(*names));
      capacity = next;
    }
    names[name_count++] = cmdb_strdup(name);
  }
  sqlite3_finalize(select);
  select = NULL;
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read legacy categories");
    goto fail;
  }

  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT id FROM ci_categories WHERE name = ? COLLATE NOCASE;",
        -1, &find, NULL) != SQLITE_OK ||
      sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE ci_types SET category = ? WHERE category = ? COLLATE NOCASE;",
        -1, &update, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare category migration");
    goto fail;
  }
  for (i = 0; i < name_count; i++) {
    char category_id[37];
    sqlite3_reset(find);
    sqlite3_clear_bindings(find);
    sqlite3_bind_text(find, 1, names[i], -1, SQLITE_TRANSIENT);
    if (sqlite3_step(find) == SQLITE_ROW) {
      (void)cmdb_str_copy(category_id, sizeof(category_id),
                          (const char *)sqlite3_column_text(find, 0));
    } else {
      if (cmdb_random_uuid(category_id) != 0) {
        cmdb_error_set(err, CMDB_ERR_INTERNAL,
                       "Cannot generate category ID");
        goto fail;
      }
      sqlite3_reset(insert);
      sqlite3_clear_bindings(insert);
      sqlite3_bind_text(insert, 1, category_id, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 2, names[i], -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 3, "Migrated category", -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 4, "", -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(insert, 5, 100);
      sqlite3_bind_text(insert, 6, now, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert, 7, now, -1, SQLITE_TRANSIENT);
      if (sqlite3_step(insert) != SQLITE_DONE) {
        cmdb_error_set(err, CMDB_ERR_DATABASE,
                       "Cannot migrate category");
        goto fail;
      }
    }
    sqlite3_reset(update);
    sqlite3_clear_bindings(update);
    sqlite3_bind_text(update, 1, category_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(update, 2, names[i], -1, SQLITE_TRANSIENT);
    if (sqlite3_step(update) != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot migrate category values");
      goto fail;
    }
  }
  for (i = 0; i < name_count; i++) {
    free(names[i]);
  }
  free(names);
  sqlite3_finalize(insert);
  sqlite3_finalize(find);
  sqlite3_finalize(update);
  return 0;

fail:
  for (i = 0; i < name_count; i++) {
    free(names[i]);
  }
  free(names);
  if (insert != NULL) {
    sqlite3_finalize(insert);
  }
  if (select != NULL) {
    sqlite3_finalize(select);
  }
  if (find != NULL) {
    sqlite3_finalize(find);
  }
  if (update != NULL) {
    sqlite3_finalize(update);
  }
  return -1;
}

static const char *seed_category_id(const char *name);

static int schema_insert_rel_types_v4(cmdb_db_t *db, const char *now,
                                      cmdb_error_t *err) {
  sqlite3_stmt *insert = NULL;
  sqlite3_stmt *update = NULL;
  size_t i;
  int rc;

  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "INSERT OR IGNORE INTO relationship_types"
        "(id, name, inverse_name, description, color, default_impact, "
        "created_at, updated_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
        -1, &insert, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare relationship type migration");
    return -1;
  }
  for (i = 0; i < sizeof(seed_rel_types) / sizeof(seed_rel_types[0]); i++) {
    sqlite3_reset(insert);
    sqlite3_clear_bindings(insert);
    sqlite3_bind_text(insert, 1, seed_rel_types[i].id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert, 2, seed_rel_types[i].name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert, 3, seed_rel_types[i].inverse, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(insert, 4, seed_rel_types[i].description, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(insert, 5, seed_rel_types[i].color, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(insert, 6, seed_rel_types[i].default_impact, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(insert, 7, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert, 8, now, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(insert) != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE,
                     "Cannot migrate relationship types");
      goto fail;
    }
  }
  sqlite3_finalize(insert);
  insert = NULL;

  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "UPDATE relationship_types SET default_impact = ? "
        "WHERE name = ? COLLATE NOCASE;",
        -1, &update, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare relationship impact migration");
    return -1;
  }
  for (i = 0; i < sizeof(seed_rel_types) / sizeof(seed_rel_types[0]); i++) {
    sqlite3_reset(update);
    sqlite3_clear_bindings(update);
    sqlite3_bind_text(update, 1, seed_rel_types[i].default_impact, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(update, 2, seed_rel_types[i].name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(update);
    if (rc != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE,
                     "Cannot migrate relationship impacts");
      goto fail;
    }
  }
  sqlite3_finalize(update);
  return 0;

fail:
  if (insert != NULL) {
    sqlite3_finalize(insert);
  }
  if (update != NULL) {
    sqlite3_finalize(update);
  }
  return -1;
}

static int schema_migrate_service_type(cmdb_db_t *db, const char *now,
                                       cmdb_error_t *err) {
  sqlite3_stmt *insert_type = NULL;
  sqlite3_stmt *find_type = NULL;
  sqlite3_stmt *insert_field = NULL;
  char service_type_id[37];
  const seed_type_t *service = NULL;
  size_t i;
  int rc;

  for (i = 0; i < sizeof(seed_types) / sizeof(seed_types[0]); i++) {
    if (strcmp(seed_types[i].name, "Service") == 0) {
      service = &seed_types[i];
      break;
    }
  }
  if (service == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Service type seed is missing");
    return -1;
  }

  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "INSERT OR IGNORE INTO ci_types"
        "(id, name, description, icon, color, category, created_at, updated_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
        -1, &insert_type, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare Service type migration");
    return -1;
  }
  sqlite3_bind_text(insert_type, 1, service->id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_type, 2, service->name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_type, 3, service->description, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_type, 4, service->icon, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_type, 5, service->color, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_type, 6, seed_category_id(service->category), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_type, 7, now, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_type, 8, now, -1, SQLITE_TRANSIENT);
  if (sqlite3_step(insert_type) != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot migrate the Service CI type");
    goto fail;
  }
  sqlite3_finalize(insert_type);
  insert_type = NULL;

  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT id FROM ci_types WHERE name = 'Service' COLLATE NOCASE;",
        -1, &find_type, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot read the Service CI type");
    return -1;
  }
  rc = sqlite3_step(find_type);
  if (rc != SQLITE_ROW) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot read the Service CI type");
    goto fail;
  }
  (void)cmdb_str_copy(service_type_id, sizeof(service_type_id),
                      (const char *)sqlite3_column_text(find_type, 0));
  sqlite3_finalize(find_type);
  find_type = NULL;

  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "INSERT OR IGNORE INTO ci_type_fields"
        "(id, type_id, field_key, label, field_type, required, options_json, "
        "sort_order) VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
        -1, &insert_field, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot prepare Service field migration");
    return -1;
  }
  for (i = 0; i < sizeof(seed_fields) / sizeof(seed_fields[0]); i++) {
    char field_id[37];
    if (strcmp(seed_fields[i].type_id, service->id) != 0) {
      continue;
    }
    if (cmdb_random_uuid(field_id) != 0) {
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot generate field ID");
      goto fail;
    }
    sqlite3_reset(insert_field);
    sqlite3_clear_bindings(insert_field);
    sqlite3_bind_text(insert_field, 1, field_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_field, 2, service_type_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_field, 3, seed_fields[i].key, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_field, 4, seed_fields[i].label, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_field, 5, seed_fields[i].field_type, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(insert_field, 6, seed_fields[i].required);
    sqlite3_bind_text(insert_field, 7,
                      seed_field_options(seed_fields[i].type_id,
                                         seed_fields[i].key),
                      -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(insert_field, 8, seed_fields[i].sort_order);
    if (sqlite3_step(insert_field) != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE,
                     "Cannot migrate Service fields");
      goto fail;
    }
  }
  sqlite3_finalize(insert_field);
  return 0;

fail:
  if (insert_type != NULL) {
    sqlite3_finalize(insert_type);
  }
  if (find_type != NULL) {
    sqlite3_finalize(find_type);
  }
  if (insert_field != NULL) {
    sqlite3_finalize(insert_field);
  }
  return -1;
}

static int schema_migrate_v4(cmdb_db_t *db, const char *now,
                             cmdb_error_t *err) {
  if (cmdb_db_exec(
        db,
        "ALTER TABLE relationship_types ADD COLUMN "
        "default_impact TEXT NOT NULL DEFAULT 'availability';",
        err) != 0 ||
      cmdb_db_exec(
        db,
        "ALTER TABLE relationships ADD COLUMN "
        "impact TEXT NOT NULL DEFAULT 'availability';",
        err) != 0) {
    return -1;
  }
  if (schema_insert_rel_types_v4(db, now, err) != 0) {
    return -1;
  }
  if (schema_migrate_service_type(db, now, err) != 0) {
    return -1;
  }
  return cmdb_db_exec(
    db,
    "UPDATE relationships SET impact = COALESCE("
    "(SELECT default_impact FROM relationship_types rt "
    " WHERE rt.id = relationships.type_id), 'availability');",
    err);
}

int cmdb_schema_migrate(cmdb_db_t *db, cmdb_error_t *err) {
  char value[64];
  int version = 0;
  size_t i;

  if (cmdb_schema_meta_get(db, "schema_version", value, sizeof(value),
                           err) != 0) {
    return -1;
  }
  if (cmdb_parse_int(value, 0, 1000000, &version) != 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Invalid schema version");
    return -1;
  }
  if (version == CMDB_SCHEMA_VERSION) {
    return 0;
  }
  if (version > CMDB_SCHEMA_VERSION || version < 1) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Unsupported database schema version");
    cmdb_error_set_detail(err, "found %d, expected %d",
                          version, CMDB_SCHEMA_VERSION);
    return -1;
  }
  if (cmdb_db_begin(db, err) != 0) {
    return -1;
  }
  if (version < 2) {
    if (cmdb_db_exec(db,
                     "ALTER TABLE ci_types ADD COLUMN "
                     "category TEXT NOT NULL DEFAULT '';",
                     err) != 0 ||
        cmdb_db_exec(db, scan_schema_sql, err) != 0) {
      (void)cmdb_db_rollback(db, NULL);
      return -1;
    }
    for (i = 0; i < sizeof(seed_types) / sizeof(seed_types[0]); i++) {
      sqlite3_stmt *stmt = NULL;
      if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                             "UPDATE ci_types SET category = ? "
                             "WHERE id = ? AND category = '';",
                             -1, &stmt, NULL) != SQLITE_OK) {
        cmdb_error_set(err, CMDB_ERR_DATABASE,
                       "Cannot migrate CI type categories");
        (void)cmdb_db_rollback(db, NULL);
        return -1;
      }
      sqlite3_bind_text(stmt, 1, seed_types[i].category, -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, seed_types[i].id, -1, SQLITE_TRANSIENT);
      if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        cmdb_error_set(err, CMDB_ERR_DATABASE,
                       "Cannot migrate CI type categories");
        (void)cmdb_db_rollback(db, NULL);
        return -1;
      }
      sqlite3_finalize(stmt);
    }
  }
  if (version < 3) {
    char now[21];
    cmdb_iso8601_now(now);
    if (schema_migrate_categories(db, now, err) != 0) {
      (void)cmdb_db_rollback(db, NULL);
      return -1;
    }
  }
  if (version < 4) {
    char now[21];
    cmdb_iso8601_now(now);
    if (schema_migrate_v4(db, now, err) != 0) {
      (void)cmdb_db_rollback(db, NULL);
      return -1;
    }
  }
  {
    char next[32];
    (void)snprintf(next, sizeof(next), "%d", CMDB_SCHEMA_VERSION);
    if (cmdb_schema_meta_set(db, "schema_version", next, err) != 0) {
      (void)cmdb_db_rollback(db, NULL);
      return -1;
    }
  }
  return cmdb_db_commit(db, err);
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

static const char *seed_category_id(const char *name) {
  size_t i;
  for (i = 0; i < sizeof(seed_categories) / sizeof(seed_categories[0]); i++) {
    if (strcmp(seed_categories[i].name, name) == 0) {
      return seed_categories[i].id;
    }
  }
  return "";
}

int cmdb_schema_seed(cmdb_db_t *db, cmdb_error_t *err) {
  sqlite3 *handle;
  sqlite3_stmt *category_stmt = NULL;
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
                         "INSERT OR IGNORE INTO ci_categories"
                         "(id, name, description, color, sort_order, "
                         "created_at, updated_at) "
                         "VALUES(?, ?, ?, ?, ?, ?, ?);",
                         -1, &category_stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot prepare category seed");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(handle));
    goto fail;
  }
  for (i = 0; i < sizeof(seed_categories) / sizeof(seed_categories[0]); i++) {
    sqlite3_reset(category_stmt);
    sqlite3_clear_bindings(category_stmt);
    sqlite3_bind_text(category_stmt, 1, seed_categories[i].id, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(category_stmt, 2, seed_categories[i].name, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(category_stmt, 3, seed_categories[i].description, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(category_stmt, 4, seed_categories[i].color, -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(category_stmt, 5, seed_categories[i].sort_order);
    sqlite3_bind_text(category_stmt, 6, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(category_stmt, 7, now, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(category_stmt) != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot seed categories");
      cmdb_error_set_detail(err, "%s", sqlite3_errmsg(handle));
      goto fail;
    }
  }
  sqlite3_finalize(category_stmt);
  category_stmt = NULL;

  if (sqlite3_prepare_v2(handle,
                         "INSERT OR IGNORE INTO ci_types"
                         "(id, name, description, icon, color, category, "
                         "created_at, updated_at) "
                         "VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
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
    sqlite3_bind_text(type_stmt, 6, seed_category_id(seed_types[i].category),
                      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(type_stmt, 7, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(type_stmt, 8, now, -1, SQLITE_TRANSIENT);
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
                         "VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
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
    sqlite3_bind_text(field_stmt, 7,
                      seed_field_options(seed_fields[i].type_id,
                                         seed_fields[i].key),
                      -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(field_stmt, 8, seed_fields[i].sort_order);
    if (sqlite3_step(field_stmt) != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot seed CI type fields");
      cmdb_error_set_detail(err, "%s", sqlite3_errmsg(handle));
      goto fail;
    }
  }

  if (sqlite3_prepare_v2(handle,
                         "INSERT OR IGNORE INTO relationship_types"
                         "(id, name, inverse_name, description, color, "
                         "default_impact, created_at, updated_at) "
                         "VALUES(?, ?, ?, ?, ?, ?, ?, ?);",
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
    sqlite3_bind_text(rel_stmt, 6, seed_rel_types[i].default_impact,
                      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(rel_stmt, 7, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(rel_stmt, 8, now, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(rel_stmt) != SQLITE_DONE) {
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot seed relationship types");
      cmdb_error_set_detail(err, "%s", sqlite3_errmsg(handle));
      goto fail;
    }
  }

  sqlite3_finalize(category_stmt);
  sqlite3_finalize(type_stmt);
  sqlite3_finalize(field_stmt);
  sqlite3_finalize(rel_stmt);
  category_stmt = NULL;
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
  if (category_stmt != NULL) {
    sqlite3_finalize(category_stmt);
  }
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
