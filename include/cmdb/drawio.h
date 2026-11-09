#ifndef CMDB_DRAWIO_H
#define CMDB_DRAWIO_H

#include "cmdb/error.h"
#include "cmdb/graph.h"

#ifdef __cplusplus
extern "C" {
#endif

int cmdb_drawio_export(const cmdb_graph_t *graph, const char *diagram_name,
                       char **xml_out, cmdb_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_DRAWIO_H */
