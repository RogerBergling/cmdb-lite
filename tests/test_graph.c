#include "test.h"
#include "test_util.h"

#include "cmdb/ci.h"
#include "cmdb/graph.h"
#include "cmdb/relationships.h"
#include "cmdb/types.h"

#include <string.h>

static void make_ci(cmdb_db_t *db, const char *type_name, const char *name,
                    char id[37], cmdb_error_t *err) {
  cmdb_ci_type_t type;
  cmdb_ci_input_t input;
  TEST_ASSERT(cmdb_types_find_by_name(db, type_name, &type, err) == 0);
  memset(&input, 0, sizeof(input));
  input.name = name;
  input.type_id = type.id;
  TEST_ASSERT(cmdb_ci_create(db, &input, id, err) == 0);
}

void test_graph(void) {
  char *dir = test_temp_dir();
  char path[4096];
  cmdb_db_t *db;
  cmdb_error_t err;
  char app_id[37];
  char server_id[37];
  char db_id[37];
  char network_id[37];
  cmdb_relationship_type_t runs_on;
  cmdb_relationship_type_t uses;
  cmdb_relationship_type_t depends_on;
  cmdb_relationship_type_t manages;
  char rel_id[37];
  char manages_id[37];
  cmdb_graph_t graph;
  const char *seeds[1];
  const char *filter[1];
  const char *impacts[1];

  TEST_ASSERT(dir != NULL);
  if (dir == NULL) {
    return;
  }
  cmdb_error_clear(&err);
  db = test_create_db(dir, "graph.cmdb", path, sizeof(path));
  TEST_ASSERT(db != NULL);
  if (db == NULL) {
    free(dir);
    return;
  }

  make_ci(db, "Web Application", "payments-api", app_id, &err);
  make_ci(db, "Linux Server", "app01", server_id, &err);
  make_ci(db, "Database", "db01", db_id, &err);
  make_ci(db, "Network", "net01", network_id, &err);

  TEST_ASSERT(cmdb_relationship_type_find(db, "runs_on", &runs_on, &err) == 0);
  TEST_ASSERT(cmdb_relationship_type_find(db, "uses", &uses, &err) == 0);
  TEST_ASSERT(cmdb_relationship_type_find(db, "depends_on", &depends_on, &err) == 0);
  TEST_ASSERT(cmdb_relationship_type_find(db, "manages", &manages, &err) == 0);

  TEST_ASSERT(cmdb_relationship_create(db, app_id, server_id, runs_on.id, "",
                                       rel_id, &err) == 0);
  TEST_ASSERT(cmdb_relationship_create(db, app_id, db_id, uses.id, "",
                                       rel_id, &err) == 0);
  TEST_ASSERT(cmdb_relationship_create(db, server_id, network_id,
                                       depends_on.id, "", rel_id, &err) == 0);
  TEST_ASSERT(cmdb_relationship_create(db, db_id, server_id,
                                       depends_on.id, "", rel_id, &err) == 0);

  seeds[0] = app_id;
  memset(&graph, 0, sizeof(graph));
  TEST_ASSERT(cmdb_graph_build(db, seeds, 1, 2, CMDB_DIRECTION_OUT,
                               NULL, 0, &graph, &err) == 0);
  TEST_ASSERT(graph.node_count == 4);
  TEST_ASSERT(graph.edge_count == 4);
  TEST_ASSERT(!graph.truncated);
  cmdb_graph_free(&graph);

  seeds[0] = server_id;
  memset(&graph, 0, sizeof(graph));
  TEST_ASSERT(cmdb_graph_build(db, seeds, 1, 1, CMDB_DIRECTION_IN,
                               NULL, 0, &graph, &err) == 0);
  TEST_ASSERT(graph.node_count == 3);
  TEST_ASSERT(graph.edge_count == 3);
  cmdb_graph_free(&graph);

  seeds[0] = db_id;
  memset(&graph, 0, sizeof(graph));
  TEST_ASSERT(cmdb_graph_build(db, seeds, 1, 2, CMDB_DIRECTION_BOTH,
                               NULL, 0, &graph, &err) == 0);
  TEST_ASSERT(graph.node_count == 4);
  TEST_ASSERT(graph.edge_count == 4);
  cmdb_graph_free(&graph);

  seeds[0] = app_id;
  filter[0] = "runs_on";
  memset(&graph, 0, sizeof(graph));
  TEST_ASSERT(cmdb_graph_build(db, seeds, 1, 2, CMDB_DIRECTION_OUT,
                               filter, 1, &graph, &err) == 0);
  TEST_ASSERT(graph.node_count == 2);
  TEST_ASSERT(graph.edge_count == 1);
  cmdb_graph_free(&graph);

  TEST_ASSERT(cmdb_relationship_create(db, app_id, network_id, manages.id,
                                       "", manages_id, &err) == 0);

  seeds[0] = app_id;
  impacts[0] = "availability";
  memset(&graph, 0, sizeof(graph));
  TEST_ASSERT(cmdb_graph_build_with_impacts(
                db, seeds, 1, 1, CMDB_DIRECTION_OUT, NULL, 0,
                impacts, 1, &graph, &err) == 0);
  TEST_ASSERT(graph.node_count == 3);
  TEST_ASSERT(graph.edge_count == 3);
  cmdb_graph_free(&graph);

  impacts[0] = "management";
  memset(&graph, 0, sizeof(graph));
  TEST_ASSERT(cmdb_graph_build_with_impacts(
                db, seeds, 1, 1, CMDB_DIRECTION_OUT, NULL, 0,
                impacts, 1, &graph, &err) == 0);
  TEST_ASSERT(graph.node_count == 2);
  TEST_ASSERT(graph.edge_count == 1);
  cmdb_graph_free(&graph);

  seeds[0] = "00000000-0000-4000-8000-000000000000";
  memset(&graph, 0, sizeof(graph));
  TEST_ASSERT(cmdb_graph_build(db, seeds, 1, 2, CMDB_DIRECTION_OUT,
                               NULL, 0, &graph, &err) != 0);
  TEST_ASSERT(err.code == CMDB_ERR_NOT_FOUND);

  cmdb_db_close(db);
  test_cleanup_db(dir, "graph.cmdb");
  free(dir);
}
