#include "cmdb/drawio.h"

#include "cmdb/util.h"

#include <string.h>

static void append_edge_label(cmdb_strbuf_t *sb, const char *type_name) {
  char buffer[CMDB_MAX_RELATIONSHIP_NAME];
  size_t i;

  if (type_name == NULL) {
    return;
  }
  (void)cmdb_str_copy(buffer, sizeof(buffer), type_name);
  for (i = 0; buffer[i] != '\0'; i++) {
    if (buffer[i] == '_') {
      buffer[i] = ' ';
    }
  }
  cmdb_strbuf_append_xml_escaped(sb, buffer);
}

int cmdb_drawio_export(const cmdb_graph_t *graph, const char *diagram_name,
                       char **xml_out, cmdb_error_t *err) {
  cmdb_strbuf_t sb;
  char diagram_id[37];
  char modified[21];
  int max_depth = 0;
  int current_depth;
  size_t i;

  if (graph == NULL || xml_out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid draw.io export");
    return -1;
  }
  *xml_out = NULL;
  if (diagram_name != NULL && strlen(diagram_name) > 255) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Diagram name is too long");
    return -1;
  }
  if (cmdb_random_uuid(diagram_id) != 0) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot generate diagram ID");
    return -1;
  }
  cmdb_iso8601_now(modified);

  for (i = 0; i < graph->node_count; i++) {
    if (graph->nodes[i].depth > max_depth) {
      max_depth = graph->nodes[i].depth;
    }
  }

  cmdb_strbuf_init(&sb);
  cmdb_strbuf_append(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  cmdb_strbuf_append(&sb,
                     "<mxfile host=\"cmdb-lite\" agent=\"cmdb-lite\" "
                     "version=\"24.0.0\" type=\"device\" modified=\"");
  cmdb_strbuf_append_xml_escaped(&sb, modified);
  cmdb_strbuf_append(&sb, "\">\n  <diagram id=\"d-");
  cmdb_strbuf_append_xml_escaped(&sb, diagram_id);
  cmdb_strbuf_append(&sb, "\" name=\"");
  cmdb_strbuf_append_xml_escaped(&sb,
                                 diagram_name != NULL ? diagram_name
                                                      : "Dependency map");
  cmdb_strbuf_append(&sb,
                     "\">\n    <mxGraphModel dx=\"1200\" dy=\"800\" grid=\"1\" "
                     "gridSize=\"10\" guides=\"1\" tooltips=\"1\" connect=\"1\" "
                     "arrows=\"1\" fold=\"1\" page=\"1\" pageScale=\"1\" "
                     "pageWidth=\"1169\" pageHeight=\"827\" math=\"0\" "
                     "shadow=\"0\">\n      <root>\n"
                     "        <mxCell id=\"0\"/>\n"
                     "        <mxCell id=\"1\" parent=\"0\"/>\n");

  for (current_depth = 0; current_depth <= max_depth; current_depth++) {
    int row = 0;
    for (i = 0; i < graph->node_count; i++) {
      const cmdb_graph_node_t *node = &graph->nodes[i];
      int x;
      int y;

      if (node->depth != current_depth) {
        continue;
      }
      x = 40 + current_depth * 280;
      y = 40 + row * 110;
      row++;

      cmdb_strbuf_append(&sb, "        <mxCell id=\"n-");
      cmdb_strbuf_append_xml_escaped(&sb, node->id);
      cmdb_strbuf_append(&sb, "\" value=\"");
      cmdb_strbuf_append_xml_escaped(&sb, node->name);
      cmdb_strbuf_append(&sb, "&#10;");
      cmdb_strbuf_append_xml_escaped(&sb, node->type_name);
      cmdb_strbuf_append(&sb, "\" style=\"rounded=1;whiteSpace=wrap;html=0;"
                              "fillColor=");
      cmdb_strbuf_append(&sb, node->is_seed ? "#d5e8d4" : "#dae8fc");
      cmdb_strbuf_append(&sb, ";strokeColor=#6c8ebf;fontSize=12;\" "
                              "vertex=\"1\" parent=\"1\">\n"
                              "          <mxGeometry x=\"");
      cmdb_strbuf_appendf(&sb, "%d", x);
      cmdb_strbuf_append(&sb, "\" y=\"");
      cmdb_strbuf_appendf(&sb, "%d", y);
      cmdb_strbuf_append(&sb,
                         "\" width=\"200\" height=\"60\" as=\"geometry\"/>\n"
                         "        </mxCell>\n");
    }
  }

  for (i = 0; i < graph->edge_count; i++) {
    const cmdb_graph_edge_t *edge = &graph->edges[i];
    cmdb_strbuf_append(&sb, "        <mxCell id=\"e-");
    cmdb_strbuf_append_xml_escaped(&sb, edge->id);
    cmdb_strbuf_append(&sb, "\" value=\"");
    append_edge_label(&sb, edge->type_name);
    cmdb_strbuf_append(&sb,
                       "\" style=\"edgeStyle=orthogonalEdgeStyle;rounded=0;"
                       "orthogonalLoop=1;jettySize=auto;html=0;"
                       "endArrow=block;endFill=1;strokeColor=#666666;\" "
                       "edge=\"1\" parent=\"1\" source=\"n-");
    cmdb_strbuf_append_xml_escaped(&sb, edge->source_id);
    cmdb_strbuf_append(&sb, "\" target=\"n-");
    cmdb_strbuf_append_xml_escaped(&sb, edge->target_id);
    cmdb_strbuf_append(&sb,
                       "\">\n          <mxGeometry relative=\"1\" "
                       "as=\"geometry\"/>\n        </mxCell>\n");
  }

  cmdb_strbuf_append(&sb,
                     "      </root>\n"
                     "    </mxGraphModel>\n"
                     "  </diagram>\n"
                     "</mxfile>\n");

  *xml_out = cmdb_strbuf_steal(&sb);
  return 0;
}
