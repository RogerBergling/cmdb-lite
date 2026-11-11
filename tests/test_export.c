#include "cmdb/ci.h"
#include "cmdb/export.h"
#include "cmdb/types.h"

#include "test.h"
#include "test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int create_ci(cmdb_db_t *db, const char *type_name, const char *name,
                     const char *external_id, const char *environment,
                     const char *status, const char *criticality,
                     const char *support_team,
                     const cmdb_attr_input_t *attrs, size_t attr_count,
                     char out_id[37]) {
  cmdb_ci_type_t type;
  cmdb_ci_input_t input;
  cmdb_error_t err;

  cmdb_error_clear(&err);
  if (cmdb_types_find_by_name(db, type_name, &type, &err) != 0) {
    (void)fprintf(stderr, "type %s not found: %s\n", type_name, err.message);
    return -1;
  }
  memset(&input, 0, sizeof(input));
  input.name = name;
  input.type_id = type.id;
  input.external_id = external_id;
  input.environment = environment;
  input.status = status;
  input.criticality = criticality;
  input.support_team = support_team;
  input.attrs = attrs;
  input.attr_count = attr_count;
  if (cmdb_ci_create(db, &input, out_id, &err) != 0) {
    (void)fprintf(stderr, "create %s failed: %s %s\n", name, err.message,
                  err.detail);
    return -1;
  }
  return 0;
}

void test_export(void) {
  char *dir = test_temp_dir();
  char db_path[4096];
  char csv_path[4096];
  cmdb_db_t *db;
  cmdb_hibp_export_options_t options;
  cmdb_hibp_export_stats_t stats;
  cmdb_error_t err;
  char web_id[37];
  char virt_id[37];
  cmdb_attr_input_t web_attrs[] = {
    {"ip_address", "10.0.0.10"},
    {"hostname", "web01.example.com"},
    {"os_version", "Ubuntu 24.04"},
    {"ssh_username", "deploy"},
    {"ssh_port", "2222"},
    {"ssh_auth_type", "key"},
    {"ssh_key_path", "/home/ops/.ssh/id_ed25519"}
  };
  cmdb_attr_input_t virt_attrs[] = {
    {"os_family", "Linux"},
    {"ip_address", "10.0.0.30"}
  };
  cmdb_attr_input_t keyless_attrs[] = {
    {"ip_address", "10.0.0.40"},
    {"ssh_auth_type", "key"}
  };

  TEST_ASSERT(dir != NULL);
  if (dir == NULL) {
    return;
  }
  db = test_create_db(dir, "export.cmdb", db_path, sizeof(db_path));
  TEST_ASSERT(db != NULL);
  if (db == NULL) {
    free(dir);
    return;
  }

  memset(web_id, 0, sizeof(web_id));
  memset(virt_id, 0, sizeof(virt_id));
  TEST_ASSERT(create_ci(db, "Linux Server", "web01", "SRV-001", "Production",
                        "Active", "High", "Linux Platform", web_attrs,
                        sizeof(web_attrs) / sizeof(web_attrs[0]), web_id) == 0);
  {
    char skipped_id[37];
    TEST_ASSERT(create_ci(db, "Linux Server", "Database Server", NULL, NULL,
                          NULL, NULL, NULL, NULL, 0, skipped_id) == 0);
  }
  {
    cmdb_attr_input_t win_attrs[] = {
      {"ip_address", "10.0.0.20"}
    };
    char win_id[37];
    TEST_ASSERT(create_ci(db, "Windows Server", "win01", NULL, NULL, NULL,
                          NULL, NULL, win_attrs,
                          sizeof(win_attrs) / sizeof(win_attrs[0]),
                          win_id) == 0);
  }
  TEST_ASSERT(create_ci(db, "Virtual Machine", "virt01", NULL, NULL, NULL,
                        NULL, NULL, virt_attrs,
                        sizeof(virt_attrs) / sizeof(virt_attrs[0]),
                        virt_id) == 0);
  {
    char keyless_id[37];
    TEST_ASSERT(create_ci(db, "Linux Server", "keyless", NULL, NULL, NULL,
                          NULL, NULL, keyless_attrs,
                          sizeof(keyless_attrs) / sizeof(keyless_attrs[0]),
                          keyless_id) == 0);
  }

  memset(&options, 0, sizeof(options));
  options.ssh_username = "ops";
  options.ssh_port = 22;
  options.ssh_auth_type = "agent";
  options.ssh_key_path = "";
  cmdb_error_clear(&err);
  test_path(csv_path, sizeof(csv_path), dir, "hibp-hosts.csv");
  TEST_ASSERT(cmdb_export_hibp_csv(db, csv_path, &options, &stats, &err) == 0);
  TEST_ASSERT(stats.exported == 2);
  TEST_ASSERT(stats.skipped == 2);

  TEST_ASSERT(test_file_contains(
    csv_path,
    "name,address,port,username,auth_type,key_path,group,tags,"
    "cmdb_ci_id,external_id,os_family,os_version,status"));
  TEST_ASSERT(test_file_contains(
    csv_path,
    "web01,10.0.0.10,2222,deploy,key,/home/ops/.ssh/id_ed25519,"
    "Production,\"cmdb-lite,Production,High,Linux Platform,Active\","));
  TEST_ASSERT(test_file_contains(csv_path, web_id));
  TEST_ASSERT(test_file_contains(csv_path,
                                 ",SRV-001,Linux,Ubuntu 24.04,Active"));
  TEST_ASSERT(test_file_contains(csv_path,
                                 "virt01,10.0.0.30,22,ops,agent,,,"
                                 "cmdb-lite,"));
  TEST_ASSERT(test_file_contains(csv_path, virt_id));
  TEST_ASSERT(!test_file_contains(csv_path, "win01"));
  TEST_ASSERT(!test_file_contains(csv_path, "Database Server"));
  TEST_ASSERT(!test_file_contains(csv_path, "keyless"));

  cmdb_db_close(db);
  test_cleanup_db(dir, "export.cmdb");
  (void)remove(csv_path);
  free(dir);
}
