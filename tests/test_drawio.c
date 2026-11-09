#include "test.h"
#include "test_util.h"

#include "cmdb/ci.h"
#include "cmdb/drawio.h"
#include "cmdb/graph.h"
#include "cmdb/relationships.h"
#include "cmdb/types.h"

#include <string.h>

void test_drawio(void) {
  char *dir = test_temp_dir();
  char path[4096];
  cmdb_db_t *db;
  cmdb_error_t err;
  cmdb_ci_type_t type;
  cmdb_ci_input_t input;
  cmdb_relationship_type_t runs_on;
  char app_id[37];
  char server_id[37];
  char rel_id[37];
  cmdb_graph_t graph;
  const char *seeds[1];
  char *xml = NULL;

  TEST_ASSERT(dir != NULL);
  if (dir == NULL) {
    return;
  }
  cmdb_error_clear(&err);
  db = test_create_db(dir, "drawio.cmdb", path, sizeof(path));
  TEST_ASSERT(db != NULL);
  if (db == NULL) {
    free(dir);
    return;
  }

  TEST_ASSERT(cmdb_types_find_by_name(db, "Web Application", &type, &err) == 0);
  memset(&input, 0, sizeof(input));
  input.name = "App & Service";
  input.type_id = type.id;
  TEST_ASSERT(cmdb_ci_create(db, &input, app_id, &err) == 0);

  TEST_ASSERT(cmdb_types_find_by_name(db, "Linux Server", &type, &err) == 0);
  input.name = "app01";
  input.type_id = type.id;
  TEST_ASSERT(cmdb_ci_create(db, &input, server_id, &err) == 0);

  TEST_ASSERT(cmdb_relationship_type_find(db, "runs_on", &runs_on, &err) == 0);
  TEST_ASSERT(cmdb_relationship_create(db, app_id, server_id, runs_on.id,
                                       "", rel_id, &err) == 0);

  seeds[0] = app_id;
  memset(&graph, 0, sizeof(graph));
  TEST_ASSERT(cmdb_graph_build(db, seeds, 1, 1, CMDB_DIRECTION_OUT,
                               NULL, 0, &graph, &err) == 0);
  TEST_ASSERT(cmdb_drawio_export(&graph, "Test & Map", &xml, &err) == 0);
  TEST_ASSERT(xml != NULL);
  if (xml != NULL) {
    TEST_ASSERT(strstr(xml, "<mxfile") != NULL);
    TEST_ASSERT(strstr(xml, "App &amp; Service") != NULL);
    TEST_ASSERT(strstr(xml, "Test &amp; Map") != NULL);
    TEST_ASSERT(strstr(xml, "runs on") != NULL);
    TEST_ASSERT(strstr(xml, "source=\"n-") != NULL);
    free(xml);
  }
  cmdb_graph_free(&graph);

  cmdb_db_close(db);
  test_cleanup_db(dir, "drawio.cmdb");
  free(dir);
}
