#include "test.h"
#include "test_util.h"

#include "cmdb/audit.h"
#include "cmdb/ci.h"
#include "cmdb/import.h"
#include "cmdb/relationships.h"
#include "cmdb/types.h"

#include <string.h>

static int parse_table(const char *content, char delimiter,
                       cmdb_csv_table_t *table, cmdb_error_t *err) {
  return cmdb_csv_parse(content, strlen(content), delimiter, table, err);
}

void test_import(void) {
  char *dir = test_temp_dir();
  char path[4096];
  cmdb_db_t *db;
  cmdb_error_t err;
  cmdb_csv_table_t table;
  cmdb_import_options_t options;
  cmdb_import_report_t report;
  const char *ci_csv =
    "ci_name,ci_type,description,environment,external_id,custom.ip_address\n"
    "imp-app,Web Application,Initial,Production,EXT-100,10.0.0.50\n";
  const char *update_csv =
    "ci_name,ci_type,description,external_id\n"
    "imp-app,Web Application,Updated,EXT-100\n";
  const char *missing_type_csv =
    "ci_name,ci_type\n"
    "new-app,New Imported Type\n";
  const char *rel_csv =
    "source_ci,relationship_type,target_ci,description,impact\n"
    "imp-app,runs_on,db01,Imported relationship,management\n";
  const char *rel_update_csv =
    "source_ci,relationship_type,target_ci,description,impact\n"
    "imp-app,runs_on,db01,Updated relationship,monitoring\n";
  const char *rel_bad_csv =
    "source_ci,relationship_type,target_ci,impact\n"
    "imp-app,runs_on,db01,bogus\n";
  char ci_id[37];
  char db_id[37];
  cmdb_ci_t ci;
  cmdb_ci_type_t type;

  TEST_ASSERT(dir != NULL);
  if (dir == NULL) {
    return;
  }
  cmdb_error_clear(&err);
  db = test_create_db(dir, "import.cmdb", path, sizeof(path));
  TEST_ASSERT(db != NULL);
  if (db == NULL) {
    free(dir);
    return;
  }

  memset(&options, 0, sizeof(options));
  options.create_missing_types = 0;
  options.duplicate_policy = CMDB_DUPLICATE_ERROR;

  memset(&table, 0, sizeof(table));
  TEST_ASSERT(parse_table(ci_csv, ',', &table, &err) == 0);
  memset(&report, 0, sizeof(report));
  TEST_ASSERT(cmdb_import_ci_preview(db, &table, &options, &report, &err) == 0);
  TEST_ASSERT(report.error_count == 0);
  TEST_ASSERT(report.create_count == 1);
  TEST_ASSERT(report.committed == 0);
  cmdb_import_report_free(&report);

  memset(&report, 0, sizeof(report));
  TEST_ASSERT(cmdb_import_ci_commit(db, &table, &options, &report, &err) == 0);
  TEST_ASSERT(report.error_count == 0);
  TEST_ASSERT(report.create_count == 1);
  TEST_ASSERT(report.committed == 1);
  cmdb_import_report_free(&report);
  cmdb_csv_free(&table);

  TEST_ASSERT(cmdb_ci_find_by_name_unique(db, "imp-app", ci_id, &err) == 0);
  TEST_ASSERT(cmdb_ci_get(db, ci_id, &ci, &err) == 0);
  TEST_ASSERT(strcmp(ci.description, "Initial") == 0);
  TEST_ASSERT(strcmp(ci.environment, "Production") == 0);
  TEST_ASSERT(ci.attr_count == 1);
  TEST_ASSERT(strcmp(ci.attrs[0].key, "ip_address") == 0);
  TEST_ASSERT(strcmp(ci.attrs[0].value, "10.0.0.50") == 0);
  cmdb_ci_free(&ci);

  /* Duplicate policy: error. */
  memset(&table, 0, sizeof(table));
  TEST_ASSERT(parse_table(ci_csv, ',', &table, &err) == 0);
  memset(&report, 0, sizeof(report));
  TEST_ASSERT(cmdb_import_ci_preview(db, &table, &options, &report, &err) == 0);
  TEST_ASSERT(report.error_count == 1);
  cmdb_import_report_free(&report);
  memset(&report, 0, sizeof(report));
  TEST_ASSERT(cmdb_import_ci_commit(db, &table, &options, &report, &err) == 0);
  TEST_ASSERT(report.committed == 0);
  TEST_ASSERT(report.error_count == 1);
  cmdb_import_report_free(&report);
  cmdb_csv_free(&table);

  /* Duplicate policy: skip. */
  options.duplicate_policy = CMDB_DUPLICATE_SKIP;
  memset(&table, 0, sizeof(table));
  TEST_ASSERT(parse_table(ci_csv, ',', &table, &err) == 0);
  memset(&report, 0, sizeof(report));
  TEST_ASSERT(cmdb_import_ci_preview(db, &table, &options, &report, &err) == 0);
  TEST_ASSERT(report.skip_count == 1);
  cmdb_import_report_free(&report);
  cmdb_csv_free(&table);

  /* Duplicate policy: update. */
  options.duplicate_policy = CMDB_DUPLICATE_UPDATE;
  memset(&table, 0, sizeof(table));
  TEST_ASSERT(parse_table(update_csv, ',', &table, &err) == 0);
  memset(&report, 0, sizeof(report));
  TEST_ASSERT(cmdb_import_ci_commit(db, &table, &options, &report, &err) == 0);
  TEST_ASSERT(report.committed == 1);
  TEST_ASSERT(report.update_count == 1);
  cmdb_import_report_free(&report);
  cmdb_csv_free(&table);
  TEST_ASSERT(cmdb_ci_get(db, ci_id, &ci, &err) == 0);
  TEST_ASSERT(strcmp(ci.description, "Updated") == 0);
  cmdb_ci_free(&ci);

  /* Missing CI type: reject, then allow creation. */
  memset(&table, 0, sizeof(table));
  TEST_ASSERT(parse_table(missing_type_csv, ',', &table, &err) == 0);
  options.create_missing_types = 0;
  options.duplicate_policy = CMDB_DUPLICATE_ERROR;
  memset(&report, 0, sizeof(report));
  TEST_ASSERT(cmdb_import_ci_preview(db, &table, &options, &report, &err) == 0);
  TEST_ASSERT(report.error_count == 1);
  cmdb_import_report_free(&report);
  cmdb_csv_free(&table);

  memset(&table, 0, sizeof(table));
  TEST_ASSERT(parse_table(missing_type_csv, ',', &table, &err) == 0);
  options.create_missing_types = 1;
  memset(&report, 0, sizeof(report));
  TEST_ASSERT(cmdb_import_ci_commit(db, &table, &options, &report, &err) == 0);
  TEST_ASSERT(report.committed == 1);
  TEST_ASSERT(report.create_count == 1);
  TEST_ASSERT(report.type_create_count == 1);
  cmdb_import_report_free(&report);
  cmdb_csv_free(&table);
  TEST_ASSERT(cmdb_types_find_by_name(db, "New Imported Type", &type, &err) == 0);

  /* Create a target CI for relationship import. */
  TEST_ASSERT(cmdb_types_find_by_name(db, "Database", &type, &err) == 0);
  {
    cmdb_ci_input_t input;
    memset(&input, 0, sizeof(input));
    input.name = "db01";
    input.type_id = type.id;
    TEST_ASSERT(cmdb_ci_create(db, &input, db_id, &err) == 0);
  }

  memset(&table, 0, sizeof(table));
  TEST_ASSERT(parse_table(rel_csv, ',', &table, &err) == 0);
  options.create_missing_types = 0;
  options.duplicate_policy = CMDB_DUPLICATE_ERROR;
  memset(&report, 0, sizeof(report));
  TEST_ASSERT(cmdb_import_rel_preview(db, &table, &options, &report, &err) == 0);
  TEST_ASSERT(report.error_count == 0);
  TEST_ASSERT(report.create_count == 1);
  cmdb_import_report_free(&report);
  memset(&report, 0, sizeof(report));
  TEST_ASSERT(cmdb_import_rel_commit(db, &table, &options, &report, &err) == 0);
  TEST_ASSERT(report.committed == 1);
  TEST_ASSERT(report.create_count == 1);
  cmdb_import_report_free(&report);
  cmdb_csv_free(&table);

  {
    cmdb_relationship_t *rels = NULL;
    size_t count = 0;
    size_t total = 0;
    TEST_ASSERT(cmdb_relationship_list(db, ci_id, 10, 0,
                                       &rels, &count, &total, &err) == 0);
    TEST_ASSERT(total == 1);
    TEST_ASSERT(count == 1);
    TEST_ASSERT(strcmp(rels[0].type_name, "runs_on") == 0);
    TEST_ASSERT(strcmp(rels[0].impact, "management") == 0);
    cmdb_relationship_free_list(rels, count);
  }

  options.duplicate_policy = CMDB_DUPLICATE_UPDATE;
  memset(&table, 0, sizeof(table));
  TEST_ASSERT(parse_table(rel_update_csv, ',', &table, &err) == 0);
  memset(&report, 0, sizeof(report));
  TEST_ASSERT(cmdb_import_rel_commit(db, &table, &options, &report, &err) == 0);
  TEST_ASSERT(report.committed == 1);
  TEST_ASSERT(report.update_count == 1);
  cmdb_import_report_free(&report);
  cmdb_csv_free(&table);
  {
    cmdb_relationship_t *rels = NULL;
    size_t count = 0;
    size_t total = 0;
    TEST_ASSERT(cmdb_relationship_list(db, ci_id, 10, 0,
                                       &rels, &count, &total, &err) == 0);
    TEST_ASSERT(total == 1);
    TEST_ASSERT(count == 1);
    TEST_ASSERT(strcmp(rels[0].description, "Updated relationship") == 0);
    TEST_ASSERT(strcmp(rels[0].impact, "monitoring") == 0);
    cmdb_relationship_free_list(rels, count);
  }

  options.duplicate_policy = CMDB_DUPLICATE_ERROR;
  memset(&table, 0, sizeof(table));
  TEST_ASSERT(parse_table(rel_bad_csv, ',', &table, &err) == 0);
  memset(&report, 0, sizeof(report));
  TEST_ASSERT(cmdb_import_rel_preview(db, &table, &options, &report, &err) == 0);
  TEST_ASSERT(report.error_count == 1);
  cmdb_import_report_free(&report);
  cmdb_csv_free(&table);

  {
    int valid = 0;
    TEST_ASSERT(cmdb_audit_verify(db, &valid, &err) == 0);
    TEST_ASSERT(valid == 1);
  }

  cmdb_db_close(db);
  test_cleanup_db(dir, "import.cmdb");
  free(dir);
}
