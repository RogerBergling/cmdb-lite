#ifndef CMDB_GRAPH_H
#define CMDB_GRAPH_H

#include "cmdb/db.h"
#include "cmdb/error.h"
#include "cmdb/limits.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CMDB_DIRECTION_OUT = 0,
  CMDB_DIRECTION_IN = 1,
  CMDB_DIRECTION_BOTH = 2
} cmdb_direction_t;

typedef struct {
  char id[37];
  char name[CMDB_MAX_NAME];
  char type_name[CMDB_MAX_TYPE_NAME];
  int depth;
  int is_seed;
} cmdb_graph_node_t;

typedef struct {
  char id[37];
  char source_id[37];
  char target_id[37];
  char type_name[CMDB_MAX_RELATIONSHIP_NAME];
  char impact[16];
} cmdb_graph_edge_t;

typedef struct {
  cmdb_graph_node_t *nodes;
  size_t node_count;
  cmdb_graph_edge_t *edges;
  size_t edge_count;
  int truncated;
} cmdb_graph_t;

int cmdb_graph_build(cmdb_db_t *db, const char *const *seed_ids,
                     size_t seed_count, int depth, cmdb_direction_t direction,
                     const char *const *relationship_types,
                     size_t relationship_type_count, cmdb_graph_t *out,
                     cmdb_error_t *err);
int cmdb_graph_build_with_impacts(
  cmdb_db_t *db, const char *const *seed_ids, size_t seed_count, int depth,
  cmdb_direction_t direction, const char *const *relationship_types,
  size_t relationship_type_count, const char *const *impacts,
  size_t impact_count, cmdb_graph_t *out, cmdb_error_t *err);

void cmdb_graph_free(cmdb_graph_t *graph);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_GRAPH_H */
