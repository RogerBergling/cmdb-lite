#include "cmdb/graph.h"

#include "cmdb/util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char id[37];
  int depth;
} graph_queue_item_t;

static int ascii_case_equal(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
      return 0;
    }
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

static int type_allowed(const char *name, const char *const *types,
                        size_t type_count) {
  size_t i;
  if (type_count == 0) {
    return 1;
  }
  for (i = 0; i < type_count; i++) {
    if (types[i] != NULL && ascii_case_equal(name, types[i])) {
      return 1;
    }
  }
  return 0;
}

static int node_find(const cmdb_graph_t *graph, const char *id) {
  size_t i;
  for (i = 0; i < graph->node_count; i++) {
    if (strcmp(graph->nodes[i].id, id) == 0) {
      return (int)i;
    }
  }
  return -1;
}

static int edge_find(const cmdb_graph_t *graph, const char *id) {
  size_t i;
  for (i = 0; i < graph->edge_count; i++) {
    if (strcmp(graph->edges[i].id, id) == 0) {
      return (int)i;
    }
  }
  return -1;
}

static int fetch_node_info(cmdb_db_t *db, const char *id,
                           char name[CMDB_MAX_NAME],
                           char type_name[CMDB_MAX_TYPE_NAME],
                           cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (sqlite3_prepare_v2(cmdb_db_sqlite(db),
                         "SELECT c.name, t.name FROM cis c "
                         "JOIN ci_types t ON t.id = c.type_id "
                         "WHERE c.id = ?;",
                         -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read graph node");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    const unsigned char *value = sqlite3_column_text(stmt, 0);
    if (cmdb_str_copy(name, CMDB_MAX_NAME, (const char *)value) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Graph node name is too long");
      return -1;
    }
    value = sqlite3_column_text(stmt, 1);
    if (cmdb_str_copy(type_name, CMDB_MAX_TYPE_NAME,
                      (const char *)value) != 0) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Graph node type is too long");
      return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "CI not found");
    return -1;
  }
  cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read graph node");
  return -1;
}

static int node_add_with_info(cmdb_db_t *db, cmdb_graph_t *graph,
                              const char *id, int depth, int is_seed,
                              cmdb_error_t *err) {
  cmdb_graph_node_t *node;
  char name[CMDB_MAX_NAME];
  char type_name[CMDB_MAX_TYPE_NAME];

  if (node_find(graph, id) >= 0) {
    return 0;
  }
  if (graph->node_count >= CMDB_MAX_GRAPH_NODES) {
    graph->truncated = 1;
    return 0;
  }
  if (fetch_node_info(db, id, name, type_name, err) != 0) {
    return -1;
  }
  graph->nodes = cmdb_realloc(graph->nodes,
                              (graph->node_count + 1) * sizeof(*graph->nodes));
  node = &graph->nodes[graph->node_count];
  memset(node, 0, sizeof(*node));
  (void)cmdb_str_copy(node->id, sizeof(node->id), id);
  (void)cmdb_str_copy(node->name, sizeof(node->name), name);
  (void)cmdb_str_copy(node->type_name, sizeof(node->type_name), type_name);
  node->depth = depth;
  node->is_seed = is_seed;
  graph->node_count++;
  return 1;
}

static int edge_add(cmdb_graph_t *graph, const char *id,
                    const char *source_id, const char *target_id,
                    const char *type_name) {
  cmdb_graph_edge_t *edge;

  if (edge_find(graph, id) >= 0) {
    return 0;
  }
  if (graph->edge_count >= CMDB_MAX_GRAPH_EDGES) {
    graph->truncated = 1;
    return 0;
  }
  graph->edges = cmdb_realloc(graph->edges,
                              (graph->edge_count + 1) * sizeof(*graph->edges));
  edge = &graph->edges[graph->edge_count];
  memset(edge, 0, sizeof(*edge));
  (void)cmdb_str_copy(edge->id, sizeof(edge->id), id);
  (void)cmdb_str_copy(edge->source_id, sizeof(edge->source_id), source_id);
  (void)cmdb_str_copy(edge->target_id, sizeof(edge->target_id), target_id);
  (void)cmdb_str_copy(edge->type_name, sizeof(edge->type_name), type_name);
  graph->edge_count++;
  return 1;
}

typedef int (*edge_callback_fn)(const char *edge_id, const char *other_id,
                                const char *type_name, void *context,
                                cmdb_error_t *err);

static int query_edges(cmdb_db_t *db, const char *node_id, int incoming,
                       edge_callback_fn callback, void *context,
                       cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  const char *sql;
  int rc;

  if (incoming) {
    sql = "SELECT r.id, r.source_id, rt.name FROM relationships r "
          "JOIN relationship_types rt ON rt.id = r.type_id "
          "WHERE r.target_id = ?;";
  } else {
    sql = "SELECT r.id, r.target_id, rt.name FROM relationships r "
          "JOIN relationship_types rt ON rt.id = r.type_id "
          "WHERE r.source_id = ?;";
  }
  if (sqlite3_prepare_v2(cmdb_db_sqlite(db), sql, -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read graph relationships");
    return -1;
  }
  sqlite3_bind_text(stmt, 1, node_id, -1, SQLITE_TRANSIENT);
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const char *edge_id = (const char *)sqlite3_column_text(stmt, 0);
    const char *other_id = (const char *)sqlite3_column_text(stmt, 1);
    const char *type_name = (const char *)sqlite3_column_text(stmt, 2);
    if (edge_id == NULL || other_id == NULL || type_name == NULL) {
      sqlite3_finalize(stmt);
      cmdb_error_set(err, CMDB_ERR_DATABASE, "Graph relationship is corrupt");
      return -1;
    }
    if (callback(edge_id, other_id, type_name, context, err) != 0) {
      sqlite3_finalize(stmt);
      return -1;
    }
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot read graph relationships");
    return -1;
  }
  return 0;
}

typedef struct {
  cmdb_db_t *db;
  cmdb_graph_t *graph;
  graph_queue_item_t *queue;
  size_t *tail;
  int current_depth;
  int max_depth;
  const char *const *relationship_types;
  size_t relationship_type_count;
  cmdb_error_t *err;
} bfs_context_t;

static int bfs_visit(const char *edge_id, const char *other_id,
                     const char *type_name, void *context,
                     cmdb_error_t *err) {
  bfs_context_t *ctx = context;
  int added;
  (void)edge_id;

  if (!type_allowed(type_name, ctx->relationship_types,
                    ctx->relationship_type_count)) {
    return 0;
  }
  if (ctx->graph->truncated) {
    return 0;
  }
  if (node_find(ctx->graph, other_id) >= 0) {
    return 0;
  }
  added = node_add_with_info(ctx->db, ctx->graph, other_id,
                             ctx->current_depth + 1, 0, err);
  if (added < 0) {
    return -1;
  }
  if (added > 0) {
    if (*ctx->tail >= CMDB_MAX_GRAPH_NODES) {
      ctx->graph->truncated = 1;
      return 0;
    }
    memset(&ctx->queue[*ctx->tail], 0, sizeof(ctx->queue[*ctx->tail]));
    (void)cmdb_str_copy(ctx->queue[*ctx->tail].id,
                        sizeof(ctx->queue[*ctx->tail].id), other_id);
    ctx->queue[*ctx->tail].depth = ctx->current_depth + 1;
    (*ctx->tail)++;
  }
  return 0;
}

typedef struct {
  cmdb_graph_t *graph;
  const char *node_id;
  int incoming;
  const char *const *relationship_types;
  size_t relationship_type_count;
} collect_context_t;

static int collect_visit(const char *edge_id, const char *other_id,
                         const char *type_name, void *context,
                         cmdb_error_t *err) {
  collect_context_t *ctx = context;
  (void)err;

  if (!type_allowed(type_name, ctx->relationship_types,
                    ctx->relationship_type_count)) {
    return 0;
  }
  if (node_find(ctx->graph, other_id) < 0) {
    return 0;
  }
  if (ctx->incoming) {
    (void)edge_add(ctx->graph, edge_id, other_id, ctx->node_id, type_name);
  } else {
    (void)edge_add(ctx->graph, edge_id, ctx->node_id, other_id, type_name);
  }
  return 0;
}

int cmdb_graph_build(cmdb_db_t *db, const char *const *seed_ids,
                     size_t seed_count, int depth, cmdb_direction_t direction,
                     const char *const *relationship_types,
                     size_t relationship_type_count, cmdb_graph_t *out,
                     cmdb_error_t *err) {
  graph_queue_item_t *queue = NULL;
  size_t head = 0;
  size_t tail = 0;
  size_t i;

  if (db == NULL || out == NULL || seed_ids == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid graph arguments");
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (seed_count == 0 || seed_count > CMDB_MAX_GRAPH_SEEDS) {
    cmdb_error_set(err, CMDB_ERR_LIMIT,
                   "Select between 1 and %u CIs", (unsigned)CMDB_MAX_GRAPH_SEEDS);
    return -1;
  }
  if (depth < 1 || depth > CMDB_MAX_GRAPH_DEPTH) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Depth must be between 1 and %d",
                   CMDB_MAX_GRAPH_DEPTH);
    return -1;
  }
  if (direction != CMDB_DIRECTION_OUT && direction != CMDB_DIRECTION_IN &&
      direction != CMDB_DIRECTION_BOTH) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Invalid graph direction");
    return -1;
  }

  queue = cmdb_calloc(CMDB_MAX_GRAPH_NODES, sizeof(*queue));

  for (i = 0; i < seed_count; i++) {
    int added;
    if (seed_ids[i] == NULL || !cmdb_uuid_valid(seed_ids[i])) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION, "Invalid CI ID in graph request");
      goto fail;
    }
    if (node_find(out, seed_ids[i]) >= 0) {
      continue;
    }
    added = node_add_with_info(db, out, seed_ids[i], 0, 1, err);
    if (added < 0) {
      goto fail;
    }
    if (added > 0) {
      (void)cmdb_str_copy(queue[tail].id, sizeof(queue[tail].id), seed_ids[i]);
      queue[tail].depth = 0;
      tail++;
    }
  }

  while (head < tail && !out->truncated) {
    graph_queue_item_t item = queue[head++];
    bfs_context_t ctx;

    if (item.depth >= depth) {
      continue;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.db = db;
    ctx.graph = out;
    ctx.queue = queue;
    ctx.tail = &tail;
    ctx.current_depth = item.depth;
    ctx.max_depth = depth;
    ctx.relationship_types = relationship_types;
    ctx.relationship_type_count = relationship_type_count;
    ctx.err = err;

    if ((direction == CMDB_DIRECTION_OUT || direction == CMDB_DIRECTION_BOTH) &&
        query_edges(db, item.id, 0, bfs_visit, &ctx, err) != 0) {
      goto fail;
    }
    if ((direction == CMDB_DIRECTION_IN || direction == CMDB_DIRECTION_BOTH) &&
        query_edges(db, item.id, 1, bfs_visit, &ctx, err) != 0) {
      goto fail;
    }
  }

  for (i = 0; i < out->node_count; i++) {
    collect_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.graph = out;
    ctx.node_id = out->nodes[i].id;
    ctx.relationship_types = relationship_types;
    ctx.relationship_type_count = relationship_type_count;

    if (direction == CMDB_DIRECTION_OUT || direction == CMDB_DIRECTION_BOTH) {
      ctx.incoming = 0;
      if (query_edges(db, out->nodes[i].id, 0, collect_visit, &ctx, err) != 0) {
        goto fail;
      }
    }
    if (direction == CMDB_DIRECTION_IN || direction == CMDB_DIRECTION_BOTH) {
      ctx.incoming = 1;
      if (query_edges(db, out->nodes[i].id, 1, collect_visit, &ctx, err) != 0) {
        goto fail;
      }
    }
  }

  free(queue);
  return 0;

fail:
  free(queue);
  cmdb_graph_free(out);
  return -1;
}

void cmdb_graph_free(cmdb_graph_t *graph) {
  if (graph == NULL) {
    return;
  }
  free(graph->nodes);
  free(graph->edges);
  memset(graph, 0, sizeof(*graph));
}
