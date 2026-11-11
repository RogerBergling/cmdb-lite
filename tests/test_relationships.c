#include "test.h"
#include "test_util.h"

#include "cmdb/ci.h"
#include "cmdb/relationships.h"
#include "cmdb/types.h"

#include <string.h>

void test_relationships(void) {
  char *dir = test_temp_dir();
  char path[4096];
  cmdb_db_t *db;
  cmdb_error_t err;
  cmdb_ci_type_t type;
  cmdb_ci_input_t input;
  cmdb_relationship_type_t *types = NULL;
  size_t type_count = 0;
  cmdb_relationship_type_t runs_on;
  cmdb_relationship_type_t manages;
  cmdb_relationship_t *rels = NULL;
  size_t count = 0;
  size_t total = 0;
  cmdb_relationship_t rel;
  char app_id[37];
  char server_id[37];
  char rel_id[37];
  char manages_id[37];
  char duplicate_id[37];
  char self_id[37];

  TEST_ASSERT(dir != NULL);
  if (dir == NULL) {
    return;
  }
  cmdb_error_clear(&err);
  db = test_create_db(dir, "rel.cmdb", path, sizeof(path));
  TEST_ASSERT(db != NULL);
  if (db == NULL) {
    free(dir);
    return;
  }
  TEST_ASSERT(cmdb_types_find_by_name(db, "Web Application", &type, &err) == 0);
  memset(&input, 0, sizeof(input));
  input.name = "payments-api";
  input.type_id = type.id;
  TEST_ASSERT(cmdb_ci_create(db, &input, app_id, &err) == 0);

  TEST_ASSERT(cmdb_types_find_by_name(db, "Linux Server", &type, &err) == 0);
  input.name = "app01";
  input.type_id = type.id;
  TEST_ASSERT(cmdb_ci_create(db, &input, server_id, &err) == 0);

  TEST_ASSERT(cmdb_relationship_types_list(db, &types, &type_count, &err) == 0);
  TEST_ASSERT(type_count >= 10);
  cmdb_relationship_types_free(types);
  TEST_ASSERT(cmdb_relationship_type_find(db, "runs_on", &runs_on, &err) == 0);
  TEST_ASSERT(strcmp(runs_on.default_impact, "availability") == 0);
  TEST_ASSERT(cmdb_relationship_type_find(db, "manages", &manages, &err) == 0);
  TEST_ASSERT(strcmp(manages.default_impact, "management") == 0);

  TEST_ASSERT(cmdb_relationship_create(db, app_id, server_id, runs_on.id,
                                       "API runs on app01", rel_id, &err) == 0);
  TEST_ASSERT(cmdb_relationship_get(db, rel_id, &rel, &err) == 0);
  TEST_ASSERT(strcmp(rel.source_name, "payments-api") == 0);
  TEST_ASSERT(strcmp(rel.target_name, "app01") == 0);
  TEST_ASSERT(strcmp(rel.type_name, "runs_on") == 0);
  TEST_ASSERT(strcmp(rel.impact, "availability") == 0);

  TEST_ASSERT(cmdb_relationship_create(db, app_id, server_id, runs_on.id,
                                       "duplicate", duplicate_id, &err) != 0);
  TEST_ASSERT(err.code == CMDB_ERR_CONFLICT);
  TEST_ASSERT(cmdb_relationship_create(db, app_id, app_id, runs_on.id,
                                       "self", self_id, &err) != 0);

  TEST_ASSERT(cmdb_relationship_list(db, app_id, 10, 0,
                                     &rels, &count, &total, &err) == 0);
  TEST_ASSERT(total == 1);
  TEST_ASSERT(count == 1);
  cmdb_relationship_free_list(rels, count);

  TEST_ASSERT(cmdb_relationship_create(db, server_id, app_id, manages.id,
                                       "Server manages the app", manages_id,
                                       &err) == 0);
  TEST_ASSERT(cmdb_relationship_get(db, manages_id, &rel, &err) == 0);
  TEST_ASSERT(strcmp(rel.impact, "management") == 0);
  TEST_ASSERT(cmdb_relationship_set_impact(db, manages_id, "monitoring",
                                           &err) == 0);
  TEST_ASSERT(cmdb_relationship_get(db, manages_id, &rel, &err) == 0);
  TEST_ASSERT(strcmp(rel.impact, "monitoring") == 0);
  TEST_ASSERT(cmdb_relationship_set_impact(db, manages_id, "bogus", &err) != 0);
  TEST_ASSERT(cmdb_relationship_delete(db, manages_id, &err) == 0);

  TEST_ASSERT(cmdb_relationship_delete(db, rel_id, &err) == 0);
  TEST_ASSERT(cmdb_relationship_get(db, rel_id, &rel, &err) != 0);
  TEST_ASSERT(err.code == CMDB_ERR_NOT_FOUND);

  cmdb_db_close(db);
  test_cleanup_db(dir, "rel.cmdb");
  free(dir);
}
