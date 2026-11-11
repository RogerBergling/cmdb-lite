#include "cmdb/api.h"

#include "app_internal.h"
#include "cmdb/audit.h"
#include "cmdb/categories.h"
#include "cmdb/ci.h"
#include "cmdb/diagram.h"
#include "cmdb/drawio.h"
#include "cmdb/export.h"
#include "cmdb/graph.h"
#include "cmdb/import.h"
#include "cmdb/json_util.h"
#include "cmdb/relationships.h"
#include "cmdb/scan.h"
#include "cmdb/schema.h"
#include "cmdb/settings.h"
#include "cmdb/types.h"
#include "cmdb/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CMDB_VERSION
#define CMDB_VERSION "0.0.0"
#endif

static cJSON *api_error_response(cJSON *id, cmdb_error_t *err) {
  cJSON *response = cJSON_CreateObject();
  cJSON *error = cJSON_CreateObject();
  cJSON *copy;

  if (response == NULL || error == NULL) {
    cJSON_Delete(response);
    cJSON_Delete(error);
    return NULL;
  }
  cJSON_AddBoolToObject(response, "ok", 0);
  if (id != NULL) {
    copy = cJSON_Duplicate(id, 1);
    if (copy != NULL) {
      cJSON_AddItemToObject(response, "id", copy);
    }
  }
  cJSON_AddStringToObject(error, "code",
                          cmdb_status_name(err != NULL ? err->code
                                                       : CMDB_ERR_INTERNAL));
  cJSON_AddStringToObject(error, "message",
                          err != NULL && err->message[0] != '\0'
                            ? err->message
                            : "Internal error");
  if (err != NULL && err->detail[0] != '\0') {
    cJSON_AddStringToObject(error, "detail", err->detail);
  }
  if (err != NULL && err->line > 0) {
    cJSON_AddNumberToObject(error, "line", err->line);
  }
  cJSON_AddItemToObject(response, "error", error);
  return response;
}

static cJSON *api_ok_response(cJSON *request, cJSON *result) {
  cJSON *response = cJSON_CreateObject();
  cJSON *id;
  cJSON *copy;

  if (response == NULL) {
    cJSON_Delete(result);
    return NULL;
  }
  cJSON_AddBoolToObject(response, "ok", 1);
  id = cJSON_GetObjectItemCaseSensitive(request, "id");
  if (id != NULL) {
    copy = cJSON_Duplicate(id, 1);
    if (copy != NULL) {
      cJSON_AddItemToObject(response, "id", copy);
    }
  }
  cJSON_AddItemToObject(response, "result", result);
  return response;
}

static int require_db(cmdb_app_t *app, cmdb_error_t *err) {
  if (app == NULL || app->db == NULL) {
    cmdb_error_set(err, CMDB_ERR_AUTH, "No database is open");
    return -1;
  }
  return 0;
}

static int require_params(cJSON *params, cmdb_error_t *err) {
  if (!cJSON_IsObject(params)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Missing parameters");
    return -1;
  }
  return 0;
}

static int get_string(cJSON *params, const char *key, const char **value,
                      int required, cmdb_error_t *err) {
  return cmdb_json_string(params, key, value, required, err);
}

static int get_int(cJSON *params, const char *key, int default_value,
                   int min_value, int max_value, int *value,
                   cmdb_error_t *err) {
  return cmdb_json_int(params, key, default_value, min_value, max_value,
                       value, err);
}

static int get_bool(cJSON *params, const char *key, int default_value,
                    int *value, cmdb_error_t *err) {
  return cmdb_json_bool(params, key, default_value, value, err);
}

static cJSON *category_to_json(const cmdb_category_t *category) {
  cJSON *obj = cJSON_CreateObject();
  if (obj == NULL) {
    return NULL;
  }
  cJSON_AddStringToObject(obj, "id", category->id);
  cJSON_AddStringToObject(obj, "name", category->name);
  cJSON_AddStringToObject(obj, "description", category->description);
  cJSON_AddStringToObject(obj, "color", category->color);
  cJSON_AddNumberToObject(obj, "sort_order", category->sort_order);
  cJSON_AddStringToObject(obj, "created_at", category->created_at);
  cJSON_AddStringToObject(obj, "updated_at", category->updated_at);
  return obj;
}

static cJSON *handle_categories_list(cmdb_app_t *app, cmdb_error_t *err) {
  cmdb_category_t *categories = NULL;
  size_t count = 0;
  size_t i;
  cJSON *result = cJSON_CreateObject();
  cJSON *items = cJSON_CreateArray();

  if (result == NULL || items == NULL) {
    cJSON_Delete(result);
    cJSON_Delete(items);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create category list");
    return NULL;
  }
  if (cmdb_categories_list(app->db, &categories, &count, err) != 0) {
    cJSON_Delete(result);
    cJSON_Delete(items);
    return NULL;
  }
  for (i = 0; i < count; i++) {
    cJSON *item = category_to_json(&categories[i]);
    if (item != NULL) {
      cJSON_AddItemToArray(items, item);
    }
  }
  cmdb_categories_free(categories);
  cJSON_AddItemToObject(result, "items", items);
  return result;
}

static cJSON *handle_categories_create(cmdb_app_t *app, cJSON *params,
                                       cmdb_error_t *err) {
  const char *name = NULL;
  const char *description = NULL;
  const char *color = NULL;
  int sort_order = 0;
  char id[37];
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "name", &name, 1, err) != 0 ||
      get_string(params, "description", &description, 0, err) != 0 ||
      get_string(params, "color", &color, 0, err) != 0 ||
      get_int(params, "sort_order", 0, 0, 10000, &sort_order, err) != 0) {
    return NULL;
  }
  if (cmdb_categories_create(app->db, name, description, color, sort_order,
                             id, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "id", id);
  }
  return result;
}

static cJSON *handle_categories_update(cmdb_app_t *app, cJSON *params,
                                       cmdb_error_t *err) {
  const char *id = NULL;
  const char *name = NULL;
  const char *description = NULL;
  const char *color = NULL;
  int sort_order = 0;
  cmdb_category_t existing;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0 ||
      get_string(params, "name", &name, 0, err) != 0 ||
      get_string(params, "description", &description, 0, err) != 0 ||
      get_string(params, "color", &color, 0, err) != 0) {
    return NULL;
  }
  if (cmdb_categories_get(app->db, id, &existing, err) != 0) {
    return NULL;
  }
  if (get_int(params, "sort_order", existing.sort_order, 0, 10000,
              &sort_order, err) != 0) {
    return NULL;
  }
  if (cmdb_categories_update(app->db, id,
                             name != NULL ? name : existing.name,
                             description != NULL ? description
                                                 : existing.description,
                             color != NULL ? color : existing.color,
                             sort_order, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "id", id);
  }
  return result;
}

static cJSON *handle_categories_delete(cmdb_app_t *app, cJSON *params,
                                       cmdb_error_t *err) {
  const char *id = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_categories_delete(app->db, id, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddBoolToObject(result, "deleted", 1);
  }
  return result;
}

static cJSON *type_to_json(const cmdb_ci_type_t *type) {
  cJSON *obj = cJSON_CreateObject();
  if (obj == NULL) {
    return NULL;
  }
  cJSON_AddStringToObject(obj, "id", type->id);
  cJSON_AddStringToObject(obj, "name", type->name);
  cJSON_AddStringToObject(obj, "description", type->description);
  cJSON_AddStringToObject(obj, "icon", type->icon);
  cJSON_AddStringToObject(obj, "color", type->color);
  cJSON_AddStringToObject(obj, "category_id", type->category_id);
  cJSON_AddStringToObject(obj, "category", type->category);
  cJSON_AddStringToObject(obj, "created_at", type->created_at);
  cJSON_AddStringToObject(obj, "updated_at", type->updated_at);
  return obj;
}

static cJSON *field_to_json(const cmdb_type_field_t *field) {
  cJSON *obj = cJSON_CreateObject();
  cJSON *options;
  cJSON *parsed;

  if (obj == NULL) {
    return NULL;
  }
  cJSON_AddStringToObject(obj, "id", field->id);
  cJSON_AddStringToObject(obj, "type_id", field->type_id);
  cJSON_AddStringToObject(obj, "key", field->key);
  cJSON_AddStringToObject(obj, "label", field->label);
  cJSON_AddStringToObject(obj, "field_type", field->field_type);
  cJSON_AddBoolToObject(obj, "required", field->required);
  cJSON_AddNumberToObject(obj, "sort_order", field->sort_order);
  parsed = cJSON_Parse(field->options_json);
  if (parsed != NULL && cJSON_IsArray(parsed)) {
    cJSON_AddItemToObject(obj, "options", parsed);
  } else {
    cJSON_Delete(parsed);
    options = cJSON_CreateArray();
    if (options != NULL) {
      cJSON_AddItemToObject(obj, "options", options);
    }
  }
  return obj;
}

static cJSON *type_fields_to_json(cmdb_db_t *db, const char *type_id,
                                  cmdb_error_t *err) {
  cmdb_type_field_t *fields = NULL;
  size_t count = 0;
  size_t i;
  cJSON *array = cJSON_CreateArray();

  if (array == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create field list");
    return NULL;
  }
  if (cmdb_type_fields_list(db, type_id, &fields, &count, err) != 0) {
    cJSON_Delete(array);
    return NULL;
  }
  for (i = 0; i < count; i++) {
    cJSON *item = field_to_json(&fields[i]);
    if (item == NULL) {
      cmdb_type_fields_free(fields);
      cJSON_Delete(array);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create field object");
      return NULL;
    }
    cJSON_AddItemToArray(array, item);
  }
  cmdb_type_fields_free(fields);
  return array;
}

static cJSON *ci_to_json(const cmdb_ci_t *ci, int include_attrs) {
  cJSON *obj = cJSON_CreateObject();
  size_t i;

  if (obj == NULL) {
    return NULL;
  }
  cJSON_AddStringToObject(obj, "id", ci->id);
  cJSON_AddStringToObject(obj, "name", ci->name);
  cJSON_AddStringToObject(obj, "type_id", ci->type_id);
  cJSON_AddStringToObject(obj, "type_name", ci->type_name);
  cJSON_AddStringToObject(obj, "description", ci->description);
  cJSON_AddStringToObject(obj, "environment", ci->environment);
  cJSON_AddStringToObject(obj, "status", ci->status);
  cJSON_AddStringToObject(obj, "business_service", ci->business_service);
  cJSON_AddStringToObject(obj, "technical_owner", ci->technical_owner);
  cJSON_AddStringToObject(obj, "support_team", ci->support_team);
  cJSON_AddStringToObject(obj, "criticality", ci->criticality);
  cJSON_AddStringToObject(obj, "hosting_location", ci->hosting_location);
  cJSON_AddStringToObject(obj, "external_id", ci->external_id);
  cJSON_AddStringToObject(obj, "created_at", ci->created_at);
  cJSON_AddStringToObject(obj, "updated_at", ci->updated_at);
  if (include_attrs) {
    cJSON *attrs = cJSON_CreateObject();
    if (attrs == NULL) {
      cJSON_Delete(obj);
      return NULL;
    }
    for (i = 0; i < ci->attr_count; i++) {
      cJSON_AddStringToObject(attrs, ci->attrs[i].key, ci->attrs[i].value);
    }
    cJSON_AddItemToObject(obj, "attributes", attrs);
  }
  return obj;
}

static cJSON *rel_to_json(const cmdb_relationship_t *rel) {
  cJSON *obj = cJSON_CreateObject();
  if (obj == NULL) {
    return NULL;
  }
  cJSON_AddStringToObject(obj, "id", rel->id);
  cJSON_AddStringToObject(obj, "source_id", rel->source_id);
  cJSON_AddStringToObject(obj, "source_name", rel->source_name);
  cJSON_AddStringToObject(obj, "target_id", rel->target_id);
  cJSON_AddStringToObject(obj, "target_name", rel->target_name);
  cJSON_AddStringToObject(obj, "type_id", rel->type_id);
  cJSON_AddStringToObject(obj, "type_name", rel->type_name);
  cJSON_AddStringToObject(obj, "description", rel->description);
  cJSON_AddStringToObject(obj, "impact", rel->impact);
  cJSON_AddStringToObject(obj, "created_at", rel->created_at);
  cJSON_AddStringToObject(obj, "updated_at", rel->updated_at);
  return obj;
}

static cJSON *rel_type_to_json(const cmdb_relationship_type_t *type) {
  cJSON *obj = cJSON_CreateObject();
  if (obj == NULL) {
    return NULL;
  }
  cJSON_AddStringToObject(obj, "id", type->id);
  cJSON_AddStringToObject(obj, "name", type->name);
  cJSON_AddStringToObject(obj, "inverse_name", type->inverse_name);
  cJSON_AddStringToObject(obj, "description", type->description);
  cJSON_AddStringToObject(obj, "color", type->color);
  cJSON_AddStringToObject(obj, "default_impact", type->default_impact);
  return obj;
}

static cJSON *report_to_json(const cmdb_import_report_t *report,
                             const char *kind) {
  cJSON *obj = cJSON_CreateObject();
  cJSON *errors;
  size_t i;

  if (obj == NULL) {
    return NULL;
  }
  cJSON_AddStringToObject(obj, "kind", kind);
  cJSON_AddNumberToObject(obj, "rows", (double)report->row_count);
  cJSON_AddNumberToObject(obj, "created", (double)report->create_count);
  cJSON_AddNumberToObject(obj, "updated", (double)report->update_count);
  cJSON_AddNumberToObject(obj, "skipped", (double)report->skip_count);
  cJSON_AddNumberToObject(obj, "error_count", (double)report->error_count);
  cJSON_AddNumberToObject(obj, "type_create", (double)report->type_create_count);
  cJSON_AddBoolToObject(obj, "committed", report->committed);
  errors = cJSON_CreateArray();
  if (errors != NULL) {
    for (i = 0; i < report->error_stored; i++) {
      cJSON *entry = cJSON_CreateObject();
      if (entry == NULL) {
        break;
      }
      cJSON_AddNumberToObject(entry, "line", (double)report->errors[i].line);
      cJSON_AddStringToObject(entry, "column", report->errors[i].column);
      cJSON_AddStringToObject(entry, "message", report->errors[i].message);
      cJSON_AddItemToArray(errors, entry);
    }
    cJSON_AddItemToObject(obj, "errors", errors);
  }
  cJSON_AddBoolToObject(obj, "errors_truncated",
                        report->error_count > report->error_stored);
  return obj;
}

static cJSON *graph_to_json(const cmdb_graph_t *graph) {
  cJSON *obj = cJSON_CreateObject();
  cJSON *nodes;
  cJSON *edges;
  size_t i;

  if (obj == NULL) {
    return NULL;
  }
  nodes = cJSON_CreateArray();
  edges = cJSON_CreateArray();
  if (nodes == NULL || edges == NULL) {
    cJSON_Delete(nodes);
    cJSON_Delete(edges);
    cJSON_Delete(obj);
    return NULL;
  }
  for (i = 0; i < graph->node_count; i++) {
    cJSON *node = cJSON_CreateObject();
    if (node == NULL) {
      break;
    }
    cJSON_AddStringToObject(node, "id", graph->nodes[i].id);
    cJSON_AddStringToObject(node, "name", graph->nodes[i].name);
    cJSON_AddStringToObject(node, "type_name", graph->nodes[i].type_name);
    cJSON_AddNumberToObject(node, "depth", graph->nodes[i].depth);
    cJSON_AddBoolToObject(node, "seed", graph->nodes[i].is_seed);
    cJSON_AddItemToArray(nodes, node);
  }
  for (i = 0; i < graph->edge_count; i++) {
    cJSON *edge = cJSON_CreateObject();
    if (edge == NULL) {
      break;
    }
    cJSON_AddStringToObject(edge, "id", graph->edges[i].id);
    cJSON_AddStringToObject(edge, "source", graph->edges[i].source_id);
    cJSON_AddStringToObject(edge, "target", graph->edges[i].target_id);
    cJSON_AddStringToObject(edge, "type_name", graph->edges[i].type_name);
    cJSON_AddStringToObject(edge, "impact", graph->edges[i].impact);
    cJSON_AddItemToArray(edges, edge);
  }
  cJSON_AddItemToObject(obj, "nodes", nodes);
  cJSON_AddItemToObject(obj, "edges", edges);
  cJSON_AddBoolToObject(obj, "truncated", graph->truncated);
  return obj;
}

static int get_options_json(cJSON *params, const char *default_json,
                            char *out, size_t out_size, cmdb_error_t *err) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(params, "options");
  char *printed;
  int rc;

  if (item == NULL) {
    rc = cmdb_str_copy(out, out_size, default_json != NULL ? default_json : "[]");
    if (rc != 0) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION, "Field options are too long");
    }
    return rc;
  }
  if (cJSON_IsArray(item)) {
    printed = cJSON_PrintUnformatted(item);
    if (printed == NULL) {
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot encode field options");
      return -1;
    }
    rc = cmdb_str_copy(out, out_size, printed);
    free(printed);
    if (rc != 0) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION, "Field options are too long");
    }
    return rc;
  }
  if (cJSON_IsString(item) && item->valuestring != NULL) {
    if (cmdb_str_copy(out, out_size, item->valuestring) != 0) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION, "Field options are too long");
      return -1;
    }
    return 0;
  }
  if (cJSON_IsNull(item)) {
    return cmdb_str_copy(out, out_size, "[]");
  }
  cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                 "Field options must be an array or JSON string");
  return -1;
}

static int get_duplicate_policy(cJSON *params, cmdb_duplicate_policy_t *policy,
                                cmdb_error_t *err) {
  const char *value = NULL;
  *policy = CMDB_DUPLICATE_ERROR;
  if (cmdb_json_string(params, "duplicate_policy", &value, 0, err) != 0) {
    return -1;
  }
  if (value == NULL || value[0] == '\0') {
    return 0;
  }
  if (strcmp(value, "error") == 0) {
    *policy = CMDB_DUPLICATE_ERROR;
  } else if (strcmp(value, "skip") == 0) {
    *policy = CMDB_DUPLICATE_SKIP;
  } else if (strcmp(value, "update") == 0) {
    *policy = CMDB_DUPLICATE_UPDATE;
  } else {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "duplicate_policy must be error, skip, or update");
    return -1;
  }
  return 0;
}

static int parse_attributes(cJSON *params, cmdb_attr_input_t **out,
                            size_t *out_count, cmdb_error_t *err) {
  cJSON *attributes = cJSON_GetObjectItemCaseSensitive(params, "attributes");
  cJSON *child;
  cmdb_attr_input_t *attrs;
  size_t count;
  size_t index = 0;

  *out = NULL;
  *out_count = 0;
  if (attributes == NULL || cJSON_IsNull(attributes)) {
    return 0;
  }
  if (!cJSON_IsObject(attributes)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "attributes must be an object");
    return -1;
  }
  count = (size_t)cJSON_GetArraySize(attributes);
  if (count > CMDB_MAX_ATTRIBUTES) {
    cmdb_error_set(err, CMDB_ERR_LIMIT, "Too many attributes");
    return -1;
  }
  attrs = cmdb_calloc(count > 0 ? count : 1, sizeof(*attrs));
  cJSON_ArrayForEach(child, attributes) {
    size_t i;
    if (child->string == NULL) {
      continue;
    }
    if (!cJSON_IsString(child) || child->valuestring == NULL) {
      free(attrs);
      cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                     "Attribute values must be strings");
      return -1;
    }
    for (i = 0; i < index; i++) {
      if (strcmp(attrs[i].key, child->string) == 0) {
        free(attrs);
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Duplicate attribute key");
        return -1;
      }
    }
    attrs[index].key = child->string;
    attrs[index].value = child->valuestring;
    index++;
  }
  *out = attrs;
  *out_count = index;
  return 0;
}

static int build_ci_input(cJSON *params, cmdb_ci_input_t *input,
                          cmdb_attr_input_t **attrs, size_t *attr_count,
                          cmdb_error_t *err) {
  memset(input, 0, sizeof(*input));
  if (cmdb_json_string(params, "name", &input->name, 1, err) != 0 ||
      cmdb_json_string(params, "type_id", &input->type_id, 1, err) != 0 ||
      cmdb_json_string(params, "description", &input->description, 0, err) != 0 ||
      cmdb_json_string(params, "environment", &input->environment, 0, err) != 0 ||
      cmdb_json_string(params, "status", &input->status, 0, err) != 0 ||
      cmdb_json_string(params, "business_service", &input->business_service, 0, err) != 0 ||
      cmdb_json_string(params, "technical_owner", &input->technical_owner, 0, err) != 0 ||
      cmdb_json_string(params, "support_team", &input->support_team, 0, err) != 0 ||
      cmdb_json_string(params, "criticality", &input->criticality, 0, err) != 0 ||
      cmdb_json_string(params, "hosting_location", &input->hosting_location, 0, err) != 0 ||
      cmdb_json_string(params, "external_id", &input->external_id, 0, err) != 0) {
    return -1;
  }
  if (parse_attributes(params, attrs, attr_count, err) != 0) {
    return -1;
  }
  input->attrs = *attrs;
  input->attr_count = *attr_count;
  return 0;
}

static cJSON *handle_app_info(cmdb_app_t *app) {
  cJSON *result = cJSON_CreateObject();
  if (result == NULL) {
    return NULL;
  }
  cJSON_AddStringToObject(result, "version", CMDB_VERSION);
  cJSON_AddBoolToObject(result, "database_open", app->db != NULL);
  cJSON_AddStringToObject(result, "database_path",
                          app->db != NULL ? cmdb_db_path(app->db) : "");
  cJSON_AddStringToObject(result, "encryption", "SQLCipher 4 / AES-256");
  cJSON_AddStringToObject(result, "kdf", "Argon2id13");
  cJSON_AddBoolToObject(result, "scan_active",
                        cmdb_scan_manager_active(app->scan_manager));
  return result;
}

static cJSON *settings_to_json(const cmdb_settings_t *settings) {
  cJSON *result = cJSON_CreateObject();
  cJSON *recent;
  size_t i;

  if (result == NULL) {
    return NULL;
  }
  cJSON_AddStringToObject(result, "last_database", settings->last_database);
  recent = cJSON_CreateArray();
  if (recent == NULL) {
    cJSON_Delete(result);
    return NULL;
  }
  for (i = 0; i < settings->recent_count; i++) {
    cJSON_AddItemToArray(recent,
                         cJSON_CreateString(settings->recent_databases[i]));
  }
  cJSON_AddItemToObject(result, "recent_databases", recent);
  return result;
}

static void settings_remember_path(const char *path) {
  cmdb_settings_t settings;
  cmdb_error_t err;

  if (path == NULL || path[0] == '\0') {
    return;
  }
  cmdb_error_clear(&err);
  if (cmdb_settings_load(&settings, &err) != 0) {
    return;
  }
  cmdb_settings_remember(&settings, path);
  (void)cmdb_settings_save(&settings, &err);
}

static cJSON *handle_settings_get(cmdb_error_t *err) {
  cmdb_settings_t settings;
  cJSON *result;

  if (cmdb_settings_load(&settings, err) != 0) {
    return NULL;
  }
  result = settings_to_json(&settings);
  if (result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create settings result");
  }
  return result;
}

static cJSON *handle_settings_remember(cJSON *params, cmdb_error_t *err) {
  const char *path = NULL;
  cmdb_settings_t settings;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "path", &path, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_settings_load(&settings, err) != 0) {
    return NULL;
  }
  cmdb_settings_remember(&settings, path);
  if (cmdb_settings_save(&settings, err) != 0) {
    return NULL;
  }
  result = settings_to_json(&settings);
  if (result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create settings result");
  }
  return result;
}

static cJSON *handle_prefs_get(cmdb_app_t *app, cmdb_error_t *err) {  char theme[16] = "system";
  int persisted = 0;
  cJSON *result;

  (void)err;
  if (app->db != NULL) {
    cmdb_error_t local;
    cmdb_error_clear(&local);
    if (cmdb_schema_meta_get(app->db, "ui.theme", theme, sizeof(theme),
                             &local) == 0) {
      persisted = 1;
    } else {
      (void)cmdb_str_copy(theme, sizeof(theme), "system");
    }
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "theme", theme);
    cJSON_AddBoolToObject(result, "persisted", persisted);
  }
  return result;
}

static cJSON *handle_prefs_set(cmdb_app_t *app, cJSON *params,
                               cmdb_error_t *err) {
  const char *theme = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "theme", &theme, 1, err) != 0) {
    return NULL;
  }
  if (strcmp(theme, "system") != 0 && strcmp(theme, "light") != 0 &&
      strcmp(theme, "dark") != 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "theme must be system, light, or dark");
    return NULL;
  }
  if (cmdb_schema_meta_set(app->db, "ui.theme", theme, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "theme", theme);
  }
  return result;
}

static cJSON *handle_db_create(cmdb_app_t *app, cJSON *params,
                               cmdb_error_t *err) {
  const char *path = NULL;
  const char *passphrase = NULL;
  cmdb_db_t *db = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "path", &path, 1, err) != 0 ||
      get_string(params, "passphrase", &passphrase, 1, err) != 0) {
    return NULL;
  }
  if (app->db != NULL) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "Lock the current database before opening another one");
    cmdb_json_wipe_string(params, "passphrase");
    return NULL;
  }
  if (cmdb_db_create(path, passphrase, &db, err) != 0) {
    cmdb_json_wipe_string(params, "passphrase");
    return NULL;
  }
  cmdb_json_wipe_string(params, "passphrase");
  app->db = db;
  settings_remember_path(path);
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "path", path);
  }
  return result;
}

static cJSON *handle_db_open(cmdb_app_t *app, cJSON *params,
                             cmdb_error_t *err) {
  const char *path = NULL;
  const char *passphrase = NULL;
  cmdb_db_t *db = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "path", &path, 1, err) != 0 ||
      get_string(params, "passphrase", &passphrase, 1, err) != 0) {
    return NULL;
  }
  if (app->db != NULL) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "Lock the current database before opening another one");
    cmdb_json_wipe_string(params, "passphrase");
    return NULL;
  }
  if (cmdb_db_open(path, passphrase, &db, err) != 0) {
    cmdb_json_wipe_string(params, "passphrase");
    return NULL;
  }
  cmdb_json_wipe_string(params, "passphrase");
  app->db = db;
  (void)cmdb_scan_jobs_recover(app->db, NULL);
  settings_remember_path(path);
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "path", path);
    cJSON_AddBoolToObject(result, "migrated",
                          cmdb_db_was_migrated(app->db));
    if (cmdb_db_migration_backup(app->db)[0] != '\0') {
      cJSON_AddStringToObject(result, "backup_path",
                              cmdb_db_migration_backup(app->db));
    }
  }
  return result;
}

static cJSON *handle_db_lock(cmdb_app_t *app, cmdb_error_t *err) {
  cJSON *result;
  if (app->scan_manager != NULL &&
      cmdb_scan_manager_active(app->scan_manager)) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "Cancel the running scan before locking the database");
    return NULL;
  }
  if (app->db != NULL) {
    cmdb_db_close(app->db);
    app->db = NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddBoolToObject(result, "locked", 1);
  }
  return result;
}

static cJSON *handle_db_status(cmdb_app_t *app) {
  cJSON *result = cJSON_CreateObject();
  if (result == NULL) {
    return NULL;
  }
  cJSON_AddBoolToObject(result, "open", app->db != NULL);
  cJSON_AddStringToObject(result, "path",
                          app->db != NULL ? cmdb_db_path(app->db) : "");
  return result;
}

static cJSON *handle_types_list(cmdb_app_t *app, cmdb_error_t *err) {
  cmdb_ci_type_t *types = NULL;
  size_t count = 0;
  size_t i;
  cJSON *result = cJSON_CreateObject();
  cJSON *items = cJSON_CreateArray();

  if (result == NULL || items == NULL) {
    cJSON_Delete(result);
    cJSON_Delete(items);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create type list");
    return NULL;
  }
  if (cmdb_types_list(app->db, &types, &count, err) != 0) {
    cJSON_Delete(result);
    cJSON_Delete(items);
    return NULL;
  }
  for (i = 0; i < count; i++) {
    cJSON *item = type_to_json(&types[i]);
    if (item == NULL) {
      cmdb_types_free(types);
      cJSON_Delete(result);
      cJSON_Delete(items);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create type object");
      return NULL;
    }
    cJSON_AddItemToArray(items, item);
  }
  cmdb_types_free(types);
  cJSON_AddItemToObject(result, "items", items);
  return result;
}

static cJSON *handle_types_get(cmdb_app_t *app, cJSON *params,
                               cmdb_error_t *err) {
  const char *id = NULL;
  cmdb_ci_type_t type;
  cJSON *result;
  cJSON *fields;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_types_get(app->db, id, &type, err) != 0) {
    return NULL;
  }
  result = type_to_json(&type);
  if (result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create type object");
    return NULL;
  }
  fields = type_fields_to_json(app->db, id, err);
  if (fields == NULL) {
    cJSON_Delete(result);
    return NULL;
  }
  cJSON_AddItemToObject(result, "fields", fields);
  return result;
}

static int resolve_category_id(cmdb_app_t *app, const char *category_id,
                               const char *category_name,
                               char out_id[37], cmdb_error_t *err) {
  cmdb_category_t category;

  out_id[0] = '\0';
  if (category_id != NULL && category_id[0] != '\0') {
    (void)cmdb_str_copy(out_id, 37, category_id);
    return 0;
  }
  if (category_name == NULL || category_name[0] == '\0') {
    return 0;
  }
  cmdb_error_clear(err);
  if (cmdb_categories_find_by_name(app->db, category_name, &category,
                                   err) == 0) {
    (void)cmdb_str_copy(out_id, 37, category.id);
    return 0;
  }
  if (err->code != CMDB_ERR_NOT_FOUND) {
    return -1;
  }
  cmdb_error_clear(err);
  if (cmdb_categories_create(app->db, category_name, "", "", 100,
                             out_id, err) != 0) {
    return -1;
  }
  return 0;
}

static cJSON *handle_types_create(cmdb_app_t *app, cJSON *params,
                                  cmdb_error_t *err) {
  const char *name = NULL;
  const char *description = NULL;
  const char *icon = NULL;
  const char *color = NULL;
  const char *category = NULL;
  const char *category_id = NULL;
  char resolved_category[37];
  char id[37];
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "name", &name, 1, err) != 0 ||
      get_string(params, "description", &description, 0, err) != 0 ||
      get_string(params, "icon", &icon, 0, err) != 0 ||
      get_string(params, "color", &color, 0, err) != 0 ||
      get_string(params, "category", &category, 0, err) != 0 ||
      get_string(params, "category_id", &category_id, 0, err) != 0) {
    return NULL;
  }
  if (resolve_category_id(app, category_id, category, resolved_category,
                          err) != 0) {
    return NULL;
  }
  if (cmdb_types_create(app->db, name, description, icon, color,
                        resolved_category, id, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "id", id);
  }
  return result;
}

static cJSON *handle_types_update(cmdb_app_t *app, cJSON *params,
                                  cmdb_error_t *err) {
  const char *id = NULL;
  const char *name = NULL;
  const char *description = NULL;
  const char *icon = NULL;
  const char *color = NULL;
  const char *category = NULL;
  const char *category_id = NULL;
  char resolved_category[37];
  cmdb_ci_type_t existing;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0 ||
      get_string(params, "name", &name, 0, err) != 0 ||
      get_string(params, "description", &description, 0, err) != 0 ||
      get_string(params, "icon", &icon, 0, err) != 0 ||
      get_string(params, "color", &color, 0, err) != 0 ||
      get_string(params, "category", &category, 0, err) != 0 ||
      get_string(params, "category_id", &category_id, 0, err) != 0) {
    return NULL;
  }
  if (cmdb_types_get(app->db, id, &existing, err) != 0) {
    return NULL;
  }
  if (category == NULL && category_id == NULL) {
    (void)cmdb_str_copy(resolved_category, sizeof(resolved_category),
                        existing.category_id);
  } else if (resolve_category_id(app, category_id, category,
                                 resolved_category, err) != 0) {
    return NULL;
  }
  if (cmdb_types_update(app->db, id,
                        name != NULL ? name : existing.name,
                        description != NULL ? description : existing.description,
                        icon != NULL ? icon : existing.icon,
                        color != NULL ? color : existing.color,
                        resolved_category,
                        err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "id", id);
  }
  return result;
}

static cJSON *handle_types_delete(cmdb_app_t *app, cJSON *params,
                                  cmdb_error_t *err) {
  const char *id = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_types_delete(app->db, id, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddBoolToObject(result, "deleted", 1);
  }
  return result;
}

static cJSON *handle_field_add(cmdb_app_t *app, cJSON *params,
                               cmdb_error_t *err) {
  const char *type_id = NULL;
  const char *key = NULL;
  const char *label = NULL;
  const char *field_type = NULL;
  int required = 0;
  int sort_order = 0;
  char options[1024];
  cmdb_type_field_input_t input;
  char id[37];
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "type_id", &type_id, 1, err) != 0 ||
      get_string(params, "key", &key, 1, err) != 0 ||
      get_string(params, "label", &label, 1, err) != 0 ||
      get_string(params, "field_type", &field_type, 1, err) != 0 ||
      get_bool(params, "required", 0, &required, err) != 0 ||
      get_int(params, "sort_order", 0, 0, 10000, &sort_order, err) != 0 ||
      get_options_json(params, "[]", options, sizeof(options), err) != 0) {
    return NULL;
  }
  memset(&input, 0, sizeof(input));
  input.key = key;
  input.label = label;
  input.field_type = field_type;
  input.required = required;
  input.options_json = options;
  input.sort_order = sort_order;
  if (cmdb_type_field_add(app->db, type_id, &input, id, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "id", id);
  }
  return result;
}

static cJSON *handle_field_update(cmdb_app_t *app, cJSON *params,
                                  cmdb_error_t *err) {
  const char *id = NULL;
  const char *key = NULL;
  const char *label = NULL;
  const char *field_type = NULL;
  int required = 0;
  int sort_order = 0;
  char options[1024];
  cmdb_type_field_t existing;
  cmdb_type_field_input_t input;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0 ||
      get_string(params, "key", &key, 0, err) != 0 ||
      get_string(params, "label", &label, 0, err) != 0 ||
      get_string(params, "field_type", &field_type, 0, err) != 0) {
    return NULL;
  }
  if (cmdb_type_field_get(app->db, id, &existing, err) != 0) {
    return NULL;
  }
  if (get_bool(params, "required", existing.required, &required, err) != 0 ||
      get_int(params, "sort_order", existing.sort_order, 0, 10000,
              &sort_order, err) != 0 ||
      get_options_json(params, existing.options_json,
                       options, sizeof(options), err) != 0) {
    return NULL;
  }
  memset(&input, 0, sizeof(input));
  input.key = key != NULL ? key : existing.key;
  input.label = label != NULL ? label : existing.label;
  input.field_type = field_type != NULL ? field_type : existing.field_type;
  input.required = required;
  input.options_json = options;
  input.sort_order = sort_order;
  if (cmdb_type_field_update(app->db, id, &input, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "id", id);
  }
  return result;
}

static cJSON *handle_field_delete(cmdb_app_t *app, cJSON *params,
                                  cmdb_error_t *err) {
  const char *id = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_type_field_delete(app->db, id, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddBoolToObject(result, "deleted", 1);
  }
  return result;
}

static cJSON *handle_cis_list(cmdb_app_t *app, cJSON *params,
                              cmdb_error_t *err) {
  const char *query = NULL;
  const char *type_id = NULL;
  int limit = 100;
  int offset = 0;
  cmdb_ci_filter_t filter;
  cmdb_ci_t *items = NULL;
  size_t count = 0;
  size_t total = 0;
  size_t i;
  cJSON *result;
  cJSON *array;

  if (params != NULL) {
    if (get_string(params, "query", &query, 0, err) != 0 ||
        get_string(params, "type_id", &type_id, 0, err) != 0 ||
        get_int(params, "limit", 100, 1, (int)CMDB_MAX_LIST_LIMIT,
                &limit, err) != 0 ||
        get_int(params, "offset", 0, 0, 1000000, &offset, err) != 0) {
      return NULL;
    }
  }
  filter.query = query;
  filter.type_id = type_id;
  filter.limit = (size_t)limit;
  filter.offset = (size_t)offset;
  if (cmdb_ci_list(app->db, &filter, &items, &count, &total, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  array = cJSON_CreateArray();
  if (result == NULL || array == NULL) {
    cmdb_ci_free_list(items, count);
    cJSON_Delete(result);
    cJSON_Delete(array);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create CI list");
    return NULL;
  }
  for (i = 0; i < count; i++) {
    cJSON *item = ci_to_json(&items[i], 0);
    if (item == NULL) {
      cmdb_ci_free_list(items, count);
      cJSON_Delete(result);
      cJSON_Delete(array);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create CI object");
      return NULL;
    }
    cJSON_AddItemToArray(array, item);
  }
  cmdb_ci_free_list(items, count);
  cJSON_AddNumberToObject(result, "total", (double)total);
  cJSON_AddNumberToObject(result, "count", (double)count);
  cJSON_AddItemToObject(result, "items", array);
  return result;
}

static cJSON *handle_cis_get(cmdb_app_t *app, cJSON *params,
                             cmdb_error_t *err) {
  const char *id = NULL;
  cmdb_ci_t ci;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_ci_get(app->db, id, &ci, err) != 0) {
    return NULL;
  }
  result = ci_to_json(&ci, 1);
  cmdb_ci_free(&ci);
  if (result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create CI object");
  }
  return result;
}

static cJSON *handle_cis_create(cmdb_app_t *app, cJSON *params,
                                cmdb_error_t *err) {
  cmdb_ci_input_t input;
  cmdb_attr_input_t *attrs = NULL;
  size_t attr_count = 0;
  char id[37];
  cJSON *result;

  if (require_params(params, err) != 0) {
    return NULL;
  }
  if (build_ci_input(params, &input, &attrs, &attr_count, err) != 0) {
    free(attrs);
    return NULL;
  }
  if (cmdb_ci_create(app->db, &input, id, err) != 0) {
    free(attrs);
    return NULL;
  }
  free(attrs);
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "id", id);
  }
  return result;
}

static cJSON *handle_cis_update(cmdb_app_t *app, cJSON *params,
                                cmdb_error_t *err) {
  const char *id = NULL;
  cmdb_ci_input_t input;
  cmdb_attr_input_t *attrs = NULL;
  size_t attr_count = 0;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0) {
    return NULL;
  }
  if (build_ci_input(params, &input, &attrs, &attr_count, err) != 0) {
    free(attrs);
    return NULL;
  }
  if (cmdb_ci_update(app->db, id, &input, err) != 0) {
    free(attrs);
    return NULL;
  }
  free(attrs);
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "id", id);
  }
  return result;
}

static cJSON *handle_cis_delete(cmdb_app_t *app, cJSON *params,
                                cmdb_error_t *err) {
  const char *id = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_ci_delete(app->db, id, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddBoolToObject(result, "deleted", 1);
  }
  return result;
}

static cJSON *handle_rel_types(cmdb_app_t *app, cmdb_error_t *err) {
  cmdb_relationship_type_t *types = NULL;
  size_t count = 0;
  size_t i;
  cJSON *result = cJSON_CreateObject();
  cJSON *array = cJSON_CreateArray();

  if (result == NULL || array == NULL) {
    cJSON_Delete(result);
    cJSON_Delete(array);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create relationship types");
    return NULL;
  }
  if (cmdb_relationship_types_list(app->db, &types, &count, err) != 0) {
    cJSON_Delete(result);
    cJSON_Delete(array);
    return NULL;
  }
  for (i = 0; i < count; i++) {
    cJSON *item = rel_type_to_json(&types[i]);
    if (item == NULL) {
      cmdb_relationship_types_free(types);
      cJSON_Delete(result);
      cJSON_Delete(array);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create relationship type");
      return NULL;
    }
    cJSON_AddItemToArray(array, item);
  }
  cmdb_relationship_types_free(types);
  cJSON_AddItemToObject(result, "items", array);
  return result;
}

static cJSON *handle_rel_list(cmdb_app_t *app, cJSON *params,
                              cmdb_error_t *err) {
  const char *ci_id = NULL;
  int limit = 100;
  int offset = 0;
  cmdb_relationship_t *items = NULL;
  size_t count = 0;
  size_t total = 0;
  size_t i;
  cJSON *result;
  cJSON *array;

  if (params != NULL) {
    if (get_string(params, "ci_id", &ci_id, 0, err) != 0 ||
        get_int(params, "limit", 100, 1, (int)CMDB_MAX_LIST_LIMIT,
                &limit, err) != 0 ||
        get_int(params, "offset", 0, 0, 1000000, &offset, err) != 0) {
      return NULL;
    }
  }
  if (cmdb_relationship_list(app->db, ci_id, (size_t)limit, (size_t)offset,
                             &items, &count, &total, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  array = cJSON_CreateArray();
  if (result == NULL || array == NULL) {
    cmdb_relationship_free_list(items, count);
    cJSON_Delete(result);
    cJSON_Delete(array);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create relationship list");
    return NULL;
  }
  for (i = 0; i < count; i++) {
    cJSON *item = rel_to_json(&items[i]);
    if (item == NULL) {
      cmdb_relationship_free_list(items, count);
      cJSON_Delete(result);
      cJSON_Delete(array);
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create relationship");
      return NULL;
    }
    cJSON_AddItemToArray(array, item);
  }
  cmdb_relationship_free_list(items, count);
  cJSON_AddNumberToObject(result, "total", (double)total);
  cJSON_AddNumberToObject(result, "count", (double)count);
  cJSON_AddItemToObject(result, "items", array);
  return result;
}

static cJSON *handle_rel_create(cmdb_app_t *app, cJSON *params,
                                cmdb_error_t *err) {
  const char *source_id = NULL;
  const char *target_id = NULL;
  const char *type_id = NULL;
  const char *description = NULL;
  const char *impact = NULL;
  char id[37];
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "source_id", &source_id, 1, err) != 0 ||
      get_string(params, "target_id", &target_id, 1, err) != 0 ||
      get_string(params, "type_id", &type_id, 1, err) != 0 ||
      get_string(params, "description", &description, 0, err) != 0 ||
      get_string(params, "impact", &impact, 0, err) != 0) {
    return NULL;
  }
  if (cmdb_relationship_create_ex(app->db, source_id, target_id, type_id,
                                  description, impact, 1, id, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "id", id);
  }
  return result;
}

static cJSON *handle_rel_set_impact(cmdb_app_t *app, cJSON *params,
                                    cmdb_error_t *err) {
  const char *id = NULL;
  const char *impact = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0 ||
      get_string(params, "impact", &impact, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_relationship_set_impact(app->db, id, impact, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddBoolToObject(result, "updated", 1);
    cJSON_AddStringToObject(result, "impact", impact);
  }
  return result;
}

static cJSON *handle_rel_delete(cmdb_app_t *app, cJSON *params,
                                cmdb_error_t *err) {
  const char *id = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_relationship_delete(app->db, id, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddBoolToObject(result, "deleted", 1);
  }
  return result;
}

static int import_format(cJSON *params, int *kind, char *delimiter,
                         cmdb_error_t *err) {
  const char *format = NULL;
  if (get_string(params, "format", &format, 1, err) != 0) {
    return -1;
  }
  if (strcmp(format, "ci_csv") == 0) {
    *kind = 0;
    *delimiter = ',';
  } else if (strcmp(format, "ci_tsv") == 0) {
    *kind = 1;
    *delimiter = '\t';
  } else if (strcmp(format, "rel_csv") == 0) {
    *kind = 2;
    *delimiter = ',';
  } else if (strcmp(format, "rel_tsv") == 0) {
    *kind = 3;
    *delimiter = '\t';
  } else {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "format must be ci_csv, ci_tsv, rel_csv, or rel_tsv");
    return -1;
  }
  return 0;
}

static cJSON *handle_import(cmdb_app_t *app, cJSON *params, int commit,
                            cmdb_error_t *err) {
  const char *content = NULL;
  size_t content_len = 0;
  int kind = 0;
  char delimiter = ',';
  cmdb_csv_table_t table;
  cmdb_import_options_t options;
  cmdb_import_report_t report;
  cJSON *result = NULL;

  memset(&table, 0, sizeof(table));
  memset(&options, 0, sizeof(options));
  memset(&report, 0, sizeof(report));

  if (require_params(params, err) != 0 ||
      import_format(params, &kind, &delimiter, err) != 0 ||
      cmdb_json_string_len(params, "content", &content, &content_len,
                           1, err) != 0 ||
      get_bool(params, "create_missing_types", 0,
               &options.create_missing_types, err) != 0 ||
      get_duplicate_policy(params, &options.duplicate_policy, err) != 0) {
    return NULL;
  }
  if (content_len > CMDB_MAX_IMPORT_BYTES) {
    cmdb_error_set(err, CMDB_ERR_LIMIT, "Import file is too large");
    return NULL;
  }
  if (!cmdb_utf8_valid(content, content_len)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Import file must be valid UTF-8");
    return NULL;
  }
  if (cmdb_csv_parse(content, content_len, delimiter, &table, err) != 0) {
    return NULL;
  }

  if (kind == 0 || kind == 1) {
    if (commit) {
      if (cmdb_import_ci_commit(app->db, &table, &options,
                                &report, err) != 0) {
        goto cleanup;
      }
    } else {
      if (cmdb_import_ci_preview(app->db, &table, &options,
                                 &report, err) != 0) {
        goto cleanup;
      }
    }
    result = report_to_json(&report, "cis");
  } else {
    if (commit) {
      if (cmdb_import_rel_commit(app->db, &table, &options,
                                 &report, err) != 0) {
        goto cleanup;
      }
    } else {
      if (cmdb_import_rel_preview(app->db, &table, &options,
                                  &report, err) != 0) {
        goto cleanup;
      }
    }
    result = report_to_json(&report, "relationships");
  }

cleanup:
  cmdb_import_report_free(&report);
  cmdb_csv_free(&table);
  return result;
}

typedef struct {
  char **ids;
  size_t id_count;
  char **rel_types;
  size_t rel_type_count;
  char **impacts;
  size_t impact_count;
  int depth;
  cmdb_direction_t direction;
} graph_params_t;

static void graph_params_free(graph_params_t *params) {
  cmdb_json_free_string_array(params->ids, params->id_count);
  cmdb_json_free_string_array(params->rel_types, params->rel_type_count);
  cmdb_json_free_string_array(params->impacts, params->impact_count);
  memset(params, 0, sizeof(*params));
}

static int parse_graph_params(cJSON *params, graph_params_t *out,
                              cmdb_error_t *err) {
  const char *direction = NULL;
  memset(out, 0, sizeof(*out));
  out->depth = 2;
  out->direction = CMDB_DIRECTION_BOTH;

  if (require_params(params, err) != 0 ||
      cmdb_json_string_array(params, "ids", &out->ids, &out->id_count,
                             err) != 0 ||
      get_int(params, "depth", 2, 1, CMDB_MAX_GRAPH_DEPTH,
              &out->depth, err) != 0 ||
      get_string(params, "direction", &direction, 0, err) != 0 ||
      cmdb_json_string_array(params, "relationship_types",
                             &out->rel_types, &out->rel_type_count,
                             err) != 0 ||
      cmdb_json_string_array(params, "impacts", &out->impacts,
                             &out->impact_count, err) != 0) {
    graph_params_free(out);
    return -1;
  }
  {
    size_t i;
    for (i = 0; i < out->impact_count; i++) {
      if (strcmp(out->impacts[i], "__none__") == 0) {
        continue;
      }
      if (!cmdb_relationship_impact_valid(out->impacts[i])) {
        graph_params_free(out);
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Invalid relationship impact filter");
        return -1;
      }
    }
  }
  if (out->id_count == 0) {
    graph_params_free(out);
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "Select at least one CI");
    return -1;
  }
  if (out->id_count > CMDB_MAX_GRAPH_SEEDS) {
    graph_params_free(out);
    cmdb_error_set(err, CMDB_ERR_LIMIT, "Too many selected CIs");
    return -1;
  }
  if (direction != NULL) {
    if (strcmp(direction, "out") == 0 || strcmp(direction, "outgoing") == 0) {
      out->direction = CMDB_DIRECTION_OUT;
    } else if (strcmp(direction, "in") == 0 ||
               strcmp(direction, "incoming") == 0) {
      out->direction = CMDB_DIRECTION_IN;
    } else if (strcmp(direction, "both") == 0) {
      out->direction = CMDB_DIRECTION_BOTH;
    } else {
      graph_params_free(out);
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "direction must be outgoing, incoming, or both");
      return -1;
    }
  }
  return 0;
}

static cJSON *handle_graph_dependencies(cmdb_app_t *app, cJSON *params,
                                        cmdb_error_t *err) {
  graph_params_t gp;
  cmdb_graph_t graph;
  cJSON *result;

  memset(&graph, 0, sizeof(graph));
  if (parse_graph_params(params, &gp, err) != 0) {
    return NULL;
  }
  if (cmdb_graph_build_with_impacts(
        app->db, (const char *const *)gp.ids, gp.id_count, gp.depth,
        gp.direction, (const char *const *)gp.rel_types, gp.rel_type_count,
        (const char *const *)gp.impacts, gp.impact_count, &graph, err) != 0) {
    graph_params_free(&gp);
    return NULL;
  }
  result = graph_to_json(&graph);
  cmdb_graph_free(&graph);
  graph_params_free(&gp);
  if (result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create graph");
  }
  return result;
}

static int path_has_extension(const char *path, const char *extension) {
  size_t path_len;
  size_t ext_len;
  size_t i;

  if (path == NULL || extension == NULL) {
    return 0;
  }
  path_len = strlen(path);
  ext_len = strlen(extension);
  if (path_len < ext_len) {
    return 0;
  }
  for (i = 0; i < ext_len; i++) {
    char a = path[path_len - ext_len + i];
    char b = extension[i];
    if (a >= 'A' && a <= 'Z') {
      a = (char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
      b = (char)(b - 'A' + 'a');
    }
    if (a != b) {
      return 0;
    }
  }
  return 1;
}

static cJSON *handle_graph_save_drawio(cmdb_app_t *app, cJSON *params,
                                       cmdb_error_t *err) {
  graph_params_t gp;
  cmdb_graph_t graph;
  const char *name = NULL;
  char *xml = NULL;
  char *path = NULL;
  cJSON *result;

  memset(&graph, 0, sizeof(graph));
  if (parse_graph_params(params, &gp, err) != 0) {
    return NULL;
  }
  if (get_string(params, "name", &name, 0, err) != 0) {
    graph_params_free(&gp);
    return NULL;
  }
  if (cmdb_graph_build_with_impacts(
        app->db, (const char *const *)gp.ids, gp.id_count, gp.depth,
        gp.direction, (const char *const *)gp.rel_types, gp.rel_type_count,
        (const char *const *)gp.impacts, gp.impact_count, &graph, err) != 0) {
    graph_params_free(&gp);
    return NULL;
  }
  if (cmdb_drawio_export(&graph, name, &xml, err) != 0) {
    cmdb_graph_free(&graph);
    graph_params_free(&gp);
    return NULL;
  }
  cmdb_graph_free(&graph);
  graph_params_free(&gp);

  if (app->host.choose_file == NULL) {
    free(xml);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "File dialog is unavailable");
    return NULL;
  }
  if (app->host.choose_file(app->host.userdata, 1,
                            "Save draw.io diagram",
                            "draw.io files", "*.drawio",
                            "dependency-map.drawio", &path, err) != 0) {
    free(xml);
    return NULL;
  }
  if (path == NULL) {
    free(xml);
    result = cJSON_CreateObject();
    if (result != NULL) {
      cJSON_AddBoolToObject(result, "saved", 0);
    }
    return result;
  }
  {
    const char *db_path = cmdb_db_path(app->db);
    char *meta_path = cmdb_path_meta(db_path);
    int conflicts = 0;
    if (db_path != NULL && strcmp(path, db_path) == 0) {
      conflicts = 1;
    }
    if (meta_path != NULL && strcmp(path, meta_path) == 0) {
      conflicts = 1;
    }
    free(meta_path);
    if (conflicts || path_has_extension(path, ".cmdb") ||
        path_has_extension(path, ".meta")) {
      free(xml);
      free(path);
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "Choose a .drawio path, not a CMDB database path");
      return NULL;
    }
  }
  if (cmdb_file_write_atomic(path, xml, strlen(xml), 0600, err) != 0) {
    free(xml);
    free(path);
    return NULL;
  }
  free(xml);
  {
    cJSON *detail = cJSON_CreateObject();
    char *json = NULL;
    if (detail != NULL) {
      cJSON_AddStringToObject(detail, "path", path);
      json = cJSON_PrintUnformatted(detail);
      cJSON_Delete(detail);
    }
    if (json != NULL) {
      (void)cmdb_audit_add(app->db, cmdb_audit_actor(), "export", "drawio", "", json, NULL);
      free(json);
    }
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddBoolToObject(result, "saved", 1);
    cJSON_AddStringToObject(result, "path", path);
  }
  free(path);
  return result;
}

static cJSON *handle_export_save(cmdb_app_t *app, cJSON *params,
                                 cmdb_error_t *err) {
  const char *kind_text = NULL;
  cmdb_export_kind_t kind = CMDB_EXPORT_CI_CSV;
  int is_hibp = 0;
  const char *ssh_username = NULL;
  const char *ssh_auth_type = "agent";
  const char *ssh_key_path = "";
  int ssh_port = 22;
  cmdb_hibp_export_options_t hibp_options;
  cmdb_hibp_export_stats_t hibp_stats;
  const char *filter_name;
  const char *pattern;
  const char *suggested;
  char *path = NULL;
  cJSON *result;

  memset(&hibp_stats, 0, sizeof(hibp_stats));
  if (require_params(params, err) != 0 ||
      get_string(params, "kind", &kind_text, 1, err) != 0) {
    return NULL;
  }
  if (strcmp(kind_text, "ci_csv") == 0) {
    kind = CMDB_EXPORT_CI_CSV;
    filter_name = "CSV files";
    pattern = "*.csv";
    suggested = "inventory.csv";
  } else if (strcmp(kind_text, "rel_csv") == 0) {
    kind = CMDB_EXPORT_REL_CSV;
    filter_name = "CSV files";
    pattern = "*.csv";
    suggested = "relationships.csv";
  } else if (strcmp(kind_text, "json") == 0) {
    kind = CMDB_EXPORT_JSON;
    filter_name = "JSON files";
    pattern = "*.json";
    suggested = "inventory.json";
  } else if (strcmp(kind_text, "hibp_csv") == 0) {
    is_hibp = 1;
    filter_name = "CSV files";
    pattern = "*.csv";
    suggested = "hibp-linux-hosts.csv";
    if (get_string(params, "ssh_username", &ssh_username, 1, err) != 0 ||
        get_int(params, "ssh_port", 22, 1, 65535, &ssh_port, err) != 0 ||
        get_string(params, "ssh_auth_type", &ssh_auth_type, 0, err) != 0 ||
        get_string(params, "ssh_key_path", &ssh_key_path, 0, err) != 0) {
      return NULL;
    }
    if (ssh_auth_type == NULL || ssh_auth_type[0] == '\0') {
      ssh_auth_type = "agent";
    }
    if (ssh_key_path == NULL) {
      ssh_key_path = "";
    }
  } else {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "kind must be ci_csv, rel_csv, json, or hibp_csv");
    return NULL;
  }
  if (app->host.choose_file == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "File dialog is unavailable");
    return NULL;
  }
  if (app->host.choose_file(app->host.userdata, 1, "Export CMDB data",
                            filter_name, pattern, suggested, &path,
                            err) != 0) {
    return NULL;
  }
  if (path == NULL) {
    result = cJSON_CreateObject();
    if (result != NULL) {
      cJSON_AddBoolToObject(result, "saved", 0);
    }
    return result;
  }
  {
    const char *db_path = cmdb_db_path(app->db);
    char *meta_path = cmdb_path_meta(db_path);
    int conflict = 0;
    if (meta_path != NULL && strcmp(path, meta_path) == 0) {
      conflict = 1;
    }
    if (db_path != NULL && strcmp(path, db_path) == 0) {
      conflict = 1;
    }
    free(meta_path);
    if (conflict || path_has_extension(path, ".cmdb") ||
        path_has_extension(path, ".meta")) {
      free(path);
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "Choose an export path, not a CMDB database path");
      return NULL;
    }
  }
  if (is_hibp) {
    hibp_options.ssh_username = ssh_username;
    hibp_options.ssh_port = ssh_port;
    hibp_options.ssh_auth_type = ssh_auth_type;
    hibp_options.ssh_key_path = ssh_key_path;
    if (cmdb_export_hibp_csv(app->db, path, &hibp_options, &hibp_stats,
                             err) != 0) {
      free(path);
      return NULL;
    }
  } else if (cmdb_export_save(app->db, kind, path, err) != 0) {
    free(path);
    return NULL;
  }
  {
    cJSON *detail = cJSON_CreateObject();
    char *json = NULL;
    if (detail != NULL) {
      cJSON_AddStringToObject(detail, "kind", kind_text);
      cJSON_AddStringToObject(detail, "path", path);
      if (is_hibp) {
        cJSON_AddNumberToObject(detail, "exported",
                                (double)hibp_stats.exported);
        cJSON_AddNumberToObject(detail, "skipped",
                                (double)hibp_stats.skipped);
      }
      json = cJSON_PrintUnformatted(detail);
      cJSON_Delete(detail);
    }
    if (json != NULL) {
      (void)cmdb_audit_add(app->db, cmdb_audit_actor(), "export", "data", kind_text,
                           json, NULL);
      free(json);
    }
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddBoolToObject(result, "saved", 1);
    cJSON_AddStringToObject(result, "path", path);
    if (is_hibp) {
      cJSON_AddNumberToObject(result, "exported",
                              (double)hibp_stats.exported);
      cJSON_AddNumberToObject(result, "skipped",
                              (double)hibp_stats.skipped);
    }
  }
  free(path);
  return result;
}

static cJSON *handle_db_backup(cmdb_app_t *app, cJSON *params,
                               cmdb_error_t *err) {
  char *path = NULL;
  cJSON *result;

  (void)params;
  if (app->host.choose_file == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "File dialog is unavailable");
    return NULL;
  }
  if (app->host.choose_file(app->host.userdata, 1,
                            "Save encrypted database backup",
                            "CMDB database", "*.cmdb",
                            "inventory-backup.cmdb", &path, err) != 0) {
    return NULL;
  }
  if (path == NULL) {
    result = cJSON_CreateObject();
    if (result != NULL) {
      cJSON_AddBoolToObject(result, "saved", 0);
    }
    return result;
  }
  if (cmdb_db_backup(app->db, path, err) != 0) {
    free(path);
    return NULL;
  }
  {
    cJSON *detail = cJSON_CreateObject();
    char *json = NULL;
    if (detail != NULL) {
      cJSON_AddStringToObject(detail, "path", path);
      json = cJSON_PrintUnformatted(detail);
      cJSON_Delete(detail);
    }
    if (json != NULL) {
      (void)cmdb_audit_add(app->db, cmdb_audit_actor(), "backup", "database", "",
                           json, NULL);
      free(json);
    }
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddBoolToObject(result, "saved", 1);
    cJSON_AddStringToObject(result, "path", path);
  }
  free(path);
  return result;
}

static cJSON *handle_db_restore(cJSON *params, cmdb_error_t *err) {
  const char *source = NULL;
  const char *destination = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "source", &source, 1, err) != 0 ||
      get_string(params, "destination", &destination, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_db_restore(source, destination, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddBoolToObject(result, "restored", 1);
    cJSON_AddStringToObject(result, "path", destination);
  }
  return result;
}

static cJSON *handle_choose_database(cmdb_app_t *app, cJSON *params,
                                     cmdb_error_t *err) {
  const char *mode = NULL;
  const char *suggested = NULL;
  char *path = NULL;
  int save = 0;
  cJSON *result;

  if (params != NULL) {
    if (get_string(params, "mode", &mode, 0, err) != 0 ||
        get_string(params, "suggested_name", &suggested, 0, err) != 0) {
      return NULL;
    }
  }
  if (mode != NULL && strcmp(mode, "save") == 0) {
    save = 1;
  }
  if (app->host.choose_file == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "File dialog is unavailable");
    return NULL;
  }
  if (app->host.choose_file(app->host.userdata, save,
                            save ? "Create CMDB database"
                                 : "Open CMDB database",
                            "CMDB database", "*.cmdb",
                            suggested != NULL ? suggested : "inventory.cmdb",
                            &path, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    if (path != NULL) {
      cJSON_AddStringToObject(result, "path", path);
    } else {
      cJSON_AddNullToObject(result, "path");
    }
  }
  free(path);
  return result;
}

static cJSON *handle_audit_list(cmdb_app_t *app, cJSON *params,
                                cmdb_error_t *err) {
  int limit = 100;
  int offset = 0;
  cmdb_audit_entry_t *entries = NULL;
  size_t count = 0;
  size_t total = 0;
  size_t i;
  cJSON *result;
  cJSON *items;

  if (params != NULL) {
    if (get_int(params, "limit", 100, 1, (int)CMDB_MAX_LIST_LIMIT,
                &limit, err) != 0 ||
        get_int(params, "offset", 0, 0, 1000000, &offset, err) != 0) {
      return NULL;
    }
  }
  if (cmdb_audit_list(app->db, (size_t)limit, (size_t)offset, &entries,
                      &count, &total, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  items = cJSON_CreateArray();
  if (result == NULL || items == NULL) {
    cmdb_audit_entries_free(entries, count);
    cJSON_Delete(result);
    cJSON_Delete(items);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create audit list");
    return NULL;
  }
  for (i = 0; i < count; i++) {
    cJSON *item = cJSON_CreateObject();
    if (item == NULL) {
      break;
    }
    cJSON_AddNumberToObject(item, "id", (double)entries[i].id);
    cJSON_AddStringToObject(item, "ts", entries[i].ts);
    cJSON_AddStringToObject(item, "actor", entries[i].actor);
    cJSON_AddStringToObject(item, "action", entries[i].action);
    cJSON_AddStringToObject(item, "entity_type", entries[i].entity_type);
    cJSON_AddStringToObject(item, "entity_id", entries[i].entity_id);
    cJSON_AddStringToObject(item, "detail",
                            entries[i].detail != NULL ? entries[i].detail : "");
    cJSON_AddStringToObject(item, "prev_hash", entries[i].prev_hash);
    cJSON_AddStringToObject(item, "hash", entries[i].hash);
    cJSON_AddItemToArray(items, item);
  }
  cmdb_audit_entries_free(entries, count);
  cJSON_AddNumberToObject(result, "total", (double)total);
  cJSON_AddNumberToObject(result, "count", (double)count);
  cJSON_AddNumberToObject(result, "offset", (double)offset);
  cJSON_AddNumberToObject(result, "limit", (double)limit);
  cJSON_AddItemToObject(result, "items", items);
  return result;
}

static cJSON *handle_audit_verify(cmdb_app_t *app, cmdb_error_t *err) {
  int valid = 0;
  size_t count = 0;
  cJSON *result;

  if (cmdb_audit_verify(app->db, &valid, err) != 0 ||
      cmdb_audit_count(app->db, &count, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create audit result");
    return NULL;
  }
  cJSON_AddBoolToObject(result, "valid", valid);
  cJSON_AddNumberToObject(result, "count", (double)count);
  return result;
}

static cJSON *handle_auth_state_get(cmdb_app_t *app, cmdb_error_t *err) {
  char *state = cmdb_malloc(1024u * 1024u);
  cJSON *result;
  cmdb_error_t read_err;

  cmdb_error_clear(&read_err);
  state[0] = '\0';
  if (cmdb_schema_meta_get(app->db, "auth.state", state, 1024u * 1024u,
                           &read_err) != 0) {
    if (read_err.code != CMDB_ERR_NOT_FOUND) {
      *err = read_err;
      free(state);
      return NULL;
    }
    cmdb_error_clear(err);
  }
  result = cJSON_CreateObject();
  if (result == NULL) {
    free(state);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create auth state");
    return NULL;
  }
  cJSON_AddStringToObject(result, "state", state);
  free(state);
  return result;
}

static cJSON *handle_auth_state_set(cmdb_app_t *app, cJSON *params,
                                    cmdb_error_t *err) {
  const char *state = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "state", &state, 1, err) != 0) {
    return NULL;
  }
  if (strlen(state) > 1024u * 1024u) {
    cmdb_error_set(err, CMDB_ERR_LIMIT, "Auth state is too large");
    return NULL;
  }
  if (cmdb_schema_meta_set(app->db, "auth.state", state, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create auth result");
    return NULL;
  }
  cJSON_AddBoolToObject(result, "saved", 1);
  return result;
}

static cJSON *handle_auth_audit(cmdb_app_t *app, cJSON *params,
                                cmdb_error_t *err) {
  const char *actor = NULL;
  const char *action = NULL;
  const char *entity_type = NULL;
  const char *entity_id = NULL;
  const char *detail = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "actor", &actor, 1, err) != 0 ||
      get_string(params, "action", &action, 1, err) != 0 ||
      get_string(params, "entity_type", &entity_type, 0, err) != 0 ||
      get_string(params, "entity_id", &entity_id, 0, err) != 0 ||
      get_string(params, "detail", &detail, 0, err) != 0) {
    return NULL;
  }
  if (cmdb_audit_add(app->db, actor, action,
                     entity_type != NULL ? entity_type : "auth",
                     entity_id != NULL ? entity_id : "",
                     detail != NULL ? detail : "{}", err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create audit result");
    return NULL;
  }
  cJSON_AddBoolToObject(result, "saved", 1);
  return result;
}

static cJSON *handle_security_info(cmdb_app_t *app, cmdb_error_t *err) {
  const cmdb_meta_t *meta = cmdb_db_meta(app->db);
  char schema[32];
  cJSON *result;

  memset(schema, 0, sizeof(schema));
  if (cmdb_schema_meta_get(app->db, "schema_version",
                           schema, sizeof(schema), err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create security info");
    return NULL;
  }
  cJSON_AddStringToObject(result, "encryption", "SQLCipher 4 / AES-256");
  cJSON_AddStringToObject(result, "kdf", "Argon2id13");
  cJSON_AddNumberToObject(result, "opslimit", (double)meta->opslimit);
  cJSON_AddNumberToObject(result, "memlimit", (double)meta->memlimit);
  cJSON_AddStringToObject(result, "schema_version", schema);
  return result;
}

static cJSON *scan_host_to_json(const cmdb_scan_host_t *host) {
  cJSON *obj = cJSON_CreateObject();
  cJSON *ports = cJSON_CreateArray();
  cJSON *matches = cJSON_CreateArray();
  const char *cursor;
  size_t i;

  if (obj == NULL || ports == NULL || matches == NULL) {
    cJSON_Delete(obj);
    cJSON_Delete(ports);
    cJSON_Delete(matches);
    return NULL;
  }
  cJSON_AddStringToObject(obj, "asset_id", host->asset_id);
  cJSON_AddStringToObject(obj, "ip_address", host->ip_address);
  cJSON_AddStringToObject(obj, "hostname", host->hostname);
  cJSON_AddStringToObject(obj, "mac_address", host->mac_address);
  cJSON_AddStringToObject(obj, "vendor", host->vendor);
  cJSON_AddStringToObject(obj, "os_guess", host->os_guess);
  cJSON_AddStringToObject(obj, "services", host->services);
  cJSON_AddStringToObject(obj, "status", host->status);
  cJSON_AddStringToObject(obj, "review_status", host->review_status);
  cJSON_AddNumberToObject(obj, "port_count", (double)host->port_count);
  cJSON_AddBoolToObject(obj, "ports_truncated", host->ports_truncated);

  cursor = host->open_ports;
  while (cursor != NULL && *cursor != '\0') {
    const char *comma = strchr(cursor, ',');
    size_t len = comma != NULL ? (size_t)(comma - cursor) : strlen(cursor);
    char token[64];
    if (len >= sizeof(token)) {
      len = sizeof(token) - 1;
    }
    (void)memcpy(token, cursor, len);
    token[len] = '\0';
    cJSON_AddItemToArray(ports, cJSON_CreateString(token));
    if (comma == NULL) {
      break;
    }
    cursor = comma + 1;
  }
  for (i = 0; i < host->match_count; i++) {
    cJSON *match = cJSON_CreateObject();
    if (match == NULL) {
      break;
    }
    cJSON_AddStringToObject(match, "ci_id", host->matches[i].ci_id);
    cJSON_AddStringToObject(match, "ci_name", host->matches[i].ci_name);
    cJSON_AddStringToObject(match, "type_name", host->matches[i].type_name);
    cJSON_AddStringToObject(match, "reasons", host->matches[i].reasons);
    cJSON_AddNumberToObject(match, "score", host->matches[i].score);
    cJSON_AddItemToArray(matches, match);
  }
  cJSON_AddItemToObject(obj, "open_ports", ports);
  cJSON_AddItemToObject(obj, "matches", matches);
  return obj;
}

static cJSON *scan_result_to_json(const cmdb_scan_result_t *scan) {
  cJSON *obj = cJSON_CreateObject();
  cJSON *hosts = cJSON_CreateArray();
  size_t i;

  if (obj == NULL || hosts == NULL) {
    cJSON_Delete(obj);
    cJSON_Delete(hosts);
    return NULL;
  }
  cJSON_AddStringToObject(obj, "job_id", scan->job_id);
  cJSON_AddStringToObject(obj, "target", scan->target);
  cJSON_AddStringToObject(obj, "ports", scan->ports);
  cJSON_AddStringToObject(obj, "method", scan->method);
  cJSON_AddStringToObject(obj, "command", scan->command);
  cJSON_AddStringToObject(obj, "started_at", scan->started_at);
  cJSON_AddStringToObject(obj, "finished_at", scan->finished_at);
  cJSON_AddStringToObject(obj, "status", scan->status);
  cJSON_AddStringToObject(obj, "error", scan->error);
  cJSON_AddBoolToObject(obj, "service_detection", scan->service_detection);
  cJSON_AddBoolToObject(obj, "os_detection", scan->os_detection);
  cJSON_AddNumberToObject(obj, "host_count", (double)scan->host_count);
  cJSON_AddBoolToObject(obj, "truncated", scan->truncated);
  for (i = 0; i < scan->host_count; i++) {
    cJSON *item = scan_host_to_json(&scan->hosts[i]);
    if (item == NULL) {
      cJSON_Delete(obj);
      cJSON_Delete(hosts);
      return NULL;
    }
    cJSON_AddItemToArray(hosts, item);
  }
  cJSON_AddItemToObject(obj, "hosts", hosts);
  return obj;
}

static cJSON *scan_summary_to_json(const cmdb_scan_job_summary_t *job) {
  cJSON *obj = cJSON_CreateObject();
  if (obj == NULL) {
    return NULL;
  }
  cJSON_AddStringToObject(obj, "job_id", job->id);
  cJSON_AddStringToObject(obj, "target", job->target);
  cJSON_AddStringToObject(obj, "status", job->status);
  cJSON_AddStringToObject(obj, "started_at", job->started_at);
  cJSON_AddStringToObject(obj, "finished_at", job->finished_at);
  cJSON_AddNumberToObject(obj, "host_count", (double)job->host_count);
  cJSON_AddNumberToObject(obj, "pending_count", (double)job->pending_count);
  cJSON_AddBoolToObject(obj, "truncated", job->truncated);
  cJSON_AddBoolToObject(obj, "service_detection", job->service_detection);
  cJSON_AddBoolToObject(obj, "os_detection", job->os_detection);
  return obj;
}

static void scan_audit(cmdb_db_t *db, const char *action, const char *target,
                       const char *ports, const char *status,
                       const cmdb_scan_result_t *scan, const char *error) {
  cJSON *detail = cJSON_CreateObject();
  char *json;

  if (detail == NULL) {
    return;
  }
  cJSON_AddStringToObject(detail, "target", target != NULL ? target : "");
  cJSON_AddStringToObject(detail, "ports", ports != NULL ? ports : "");
  cJSON_AddStringToObject(detail, "status", status != NULL ? status : "");
  if (scan != NULL) {
    cJSON_AddStringToObject(detail, "job_id", scan->job_id);
    cJSON_AddStringToObject(detail, "command", scan->command);
    cJSON_AddStringToObject(detail, "started_at", scan->started_at);
    cJSON_AddStringToObject(detail, "finished_at", scan->finished_at);
    cJSON_AddNumberToObject(detail, "host_count", (double)scan->host_count);
  }
  if (error != NULL && error[0] != '\0') {
    cJSON_AddStringToObject(detail, "error", error);
  }
  json = cJSON_PrintUnformatted(detail);
  cJSON_Delete(detail);
  if (json == NULL) {
    return;
  }
  (void)cmdb_audit_add(db, cmdb_audit_actor(), action, "network_scan",
                       target != NULL ? target : "", json, NULL);
  free(json);
}

static cJSON *handle_scan_start(cmdb_app_t *app, cJSON *params,
                                cmdb_error_t *err) {
  const char *target = NULL;
  const char *ports = NULL;
  int authorized = 0;
  int service_detection = 0;
  int os_detection = 0;
  int timeout_seconds = CMDB_SCAN_DEFAULT_TIMEOUT;
  char normalized_target[64];
  char normalized_ports[1024];
  char job_id[37];
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "target", &target, 1, err) != 0 ||
      get_string(params, "ports", &ports, 0, err) != 0 ||
      get_bool(params, "authorized", 0, &authorized, err) != 0 ||
      get_bool(params, "service_detection", 0, &service_detection, err) != 0 ||
      get_bool(params, "os_detection", 0, &os_detection, err) != 0 ||
      get_int(params, "timeout_seconds", CMDB_SCAN_DEFAULT_TIMEOUT,
              CMDB_SCAN_MIN_TIMEOUT, CMDB_SCAN_MAX_TIMEOUT,
              &timeout_seconds, err) != 0) {
    return NULL;
  }
  if (!authorized) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "You must confirm that you are authorised to scan the target");
    return NULL;
  }
  if (ports == NULL || ports[0] == '\0') {
    ports = CMDB_SCAN_DEFAULT_PORTS;
  }
  if (cmdb_scan_validate_target(target, normalized_target, err) != 0 ||
      cmdb_scan_validate_ports(ports, normalized_ports, err) != 0) {
    return NULL;
  }
  if (cmdb_scan_manager_active(app->scan_manager)) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "A scan is already running; cancel it first");
    return NULL;
  }
  if (cmdb_scan_store_begin(app->db, normalized_target, normalized_ports,
                            service_detection, os_detection,
                            job_id, err) != 0) {
    return NULL;
  }
  if (cmdb_scan_manager_start(app->scan_manager, job_id, normalized_target,
                              normalized_ports, timeout_seconds,
                              service_detection, os_detection, err) != 0) {
    cmdb_scan_result_t failed;
    memset(&failed, 0, sizeof(failed));
    (void)cmdb_str_copy(failed.job_id, sizeof(failed.job_id), job_id);
    (void)cmdb_str_copy(failed.target, sizeof(failed.target),
                        normalized_target);
    (void)cmdb_str_copy(failed.ports, sizeof(failed.ports),
                        normalized_ports);
    (void)cmdb_str_copy(failed.status, sizeof(failed.status), "failed");
    (void)cmdb_iso8601_now(failed.finished_at);
    (void)cmdb_scan_store_finish(app->db, &failed, "failed", err->message,
                                 NULL);
    return NULL;
  }
  scan_audit(app->db, "scan.start", normalized_target, normalized_ports,
             "running", NULL, NULL);
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "job_id", job_id);
    cJSON_AddStringToObject(result, "status", "running");
    cJSON_AddStringToObject(result, "target", normalized_target);
    cJSON_AddStringToObject(result, "ports", normalized_ports);
    cJSON_AddBoolToObject(result, "service_detection", service_detection);
    cJSON_AddBoolToObject(result, "os_detection", os_detection);
  }
  return result;
}

static cJSON *scan_failed_status_json(cmdb_app_t *app, const char *job_id,
                                      const char *status, const char *error,
                                      cmdb_error_t *err) {
  cmdb_scan_result_t stored;
  cJSON *json;

  memset(&stored, 0, sizeof(stored));
  if (cmdb_scan_store_load(app->db, job_id, &stored, err) != 0) {
    return NULL;
  }
  (void)cmdb_str_copy(stored.status, sizeof(stored.status), status);
  (void)cmdb_str_copy(stored.error, sizeof(stored.error),
                      error != NULL ? error : "");
  (void)cmdb_iso8601_now(stored.finished_at);
  if (cmdb_scan_store_finish(app->db, &stored, status, error, err) != 0) {
    cmdb_scan_result_free(&stored);
    return NULL;
  }
  json = scan_result_to_json(&stored);
  cmdb_scan_result_free(&stored);
  return json;
}

static cJSON *handle_scan_status(cmdb_app_t *app, cJSON *params,
                                 cmdb_error_t *err) {
  const char *job_id = NULL;
  char status[16];
  cmdb_scan_result_t result;
  int has_result = 0;
  cJSON *json;

  if (require_params(params, err) != 0 ||
      get_string(params, "job_id", &job_id, 1, err) != 0) {
    return NULL;
  }
  memset(&result, 0, sizeof(result));
  if (cmdb_scan_manager_status(app->scan_manager, job_id, status, &result,
                               &has_result, err) == 0) {
    if (strcmp(status, "running") == 0) {
      json = cJSON_CreateObject();
      if (json != NULL) {
        cJSON_AddStringToObject(json, "job_id", job_id);
        cJSON_AddStringToObject(json, "status", "running");
      }
      cmdb_scan_result_free(&result);
      return json;
    }
    if (has_result) {
      if (cmdb_scan_store_finish(app->db, &result, "completed", NULL,
                                 err) != 0) {
        cmdb_scan_result_free(&result);
        return NULL;
      }
      scan_audit(app->db, "scan.complete", result.target, result.ports,
                 "completed", &result, NULL);
      json = scan_result_to_json(&result);
      cmdb_scan_result_free(&result);
      return json;
    }
    if (strcmp(status, "failed") == 0 ||
        strcmp(status, "cancelled") == 0) {
      json = scan_failed_status_json(app, job_id, status, result.error, err);
      cmdb_scan_result_free(&result);
      return json;
    }
    cmdb_scan_result_free(&result);
  }
  /* Completed jobs that have already been persisted, or jobs from a previous
   * process, are loaded from the staging table. */
  memset(&result, 0, sizeof(result));
  if (cmdb_scan_store_load(app->db, job_id, &result, err) != 0) {
    return NULL;
  }
  json = scan_result_to_json(&result);
  cmdb_scan_result_free(&result);
  return json;
}

static cJSON *handle_scan_cancel(cmdb_app_t *app, cJSON *params,
                                 cmdb_error_t *err) {
  const char *job_id = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "job_id", &job_id, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_scan_manager_cancel(app->scan_manager, job_id, err) != 0) {
    return NULL;
  }
  scan_audit(app->db, "scan.cancel", "", "", "cancelling", NULL, NULL);
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "job_id", job_id);
    cJSON_AddBoolToObject(result, "cancelling", 1);
  }
  return result;
}

static cJSON *handle_scan_jobs(cmdb_app_t *app, cmdb_error_t *err) {
  cmdb_scan_job_summary_t *jobs = NULL;
  size_t count = 0;
  size_t i;
  cJSON *result = cJSON_CreateObject();
  cJSON *items = cJSON_CreateArray();

  if (result == NULL || items == NULL) {
    cJSON_Delete(result);
    cJSON_Delete(items);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create scan job list");
    return NULL;
  }
  if (cmdb_scan_jobs_list(app->db, &jobs, &count, err) != 0) {
    cJSON_Delete(result);
    cJSON_Delete(items);
    return NULL;
  }
  for (i = 0; i < count; i++) {
    cJSON *item = scan_summary_to_json(&jobs[i]);
    if (item != NULL) {
      cJSON_AddItemToArray(items, item);
    }
  }
  free(jobs);
  cJSON_AddItemToObject(result, "items", items);
  return result;
}

static cJSON *handle_scan_job(cmdb_app_t *app, cJSON *params,
                              cmdb_error_t *err) {
  const char *job_id = NULL;
  cmdb_scan_result_t result;
  cJSON *json;

  if (params != NULL &&
      get_string(params, "job_id", &job_id, 0, err) != 0) {
    return NULL;
  }
  memset(&result, 0, sizeof(result));
  if (job_id != NULL && job_id[0] != '\0') {
    if (cmdb_scan_store_load(app->db, job_id, &result, err) != 0) {
      return NULL;
    }
  } else if (cmdb_scan_store_latest(app->db, &result, err) != 0) {
    return NULL;
  }
  json = scan_result_to_json(&result);
  cmdb_scan_result_free(&result);
  return json;
}

static cJSON *handle_scan_discard(cmdb_app_t *app, cJSON *params,
                                  cmdb_error_t *err) {
  const char *job_id = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "job_id", &job_id, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_scan_manager_active(app->scan_manager)) {
    char status[16];
    cmdb_scan_result_t active;
    int has_result = 0;
    if (cmdb_scan_manager_status(app->scan_manager, job_id, status, &active,
                                 &has_result, err) == 0 &&
        strcmp(status, "running") == 0) {
      cmdb_scan_result_free(&active);
      cmdb_error_set(err, CMDB_ERR_CONFLICT,
                     "Cancel the running scan before discarding it");
      return NULL;
    }
    cmdb_scan_result_free(&active);
  }
  if (cmdb_scan_jobs_delete(app->db, job_id, err) != 0) {
    return NULL;
  }
  scan_audit(app->db, "scan.discard", "", "", "discarded", NULL, NULL);
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddBoolToObject(result, "deleted", 1);
  }
  return result;
}

static int scan_host_attr(cmdb_scan_host_t *host, const char *key,
                          char *out, size_t out_size) {
  if (strcmp(key, "ip_address") == 0) {
    return cmdb_str_copy(out, out_size, host->ip_address);
  }
  if (strcmp(key, "hostname") == 0) {
    return cmdb_str_copy(out, out_size, host->hostname);
  }
  if (strcmp(key, "mac_address") == 0) {
    return cmdb_str_copy(out, out_size, host->mac_address);
  }
  if (strcmp(key, "vendor") == 0) {
    return cmdb_str_copy(out, out_size, host->vendor);
  }
  if (strcmp(key, "os_guess") == 0) {
    return cmdb_str_copy(out, out_size, host->os_guess);
  }
  if (strcmp(key, "services") == 0) {
    return cmdb_str_copy(out, out_size, host->services);
  }
  if (strcmp(key, "open_ports") == 0) {
    return cmdb_str_copy(out, out_size, host->open_ports);
  }
  return -1;
}

static int scan_create_from_host(cmdb_app_t *app, const char *type_id,
                                 const char *target, cmdb_scan_host_t *host,
                                 const char *description,
                                 const char *environment,
                                 const char *status,
                                 const char *criticality,
                                 const char *location, char id[37],
                                 cmdb_error_t *err) {
  cmdb_attr_input_t attrs[10];
  size_t attr_count = 0;
  cmdb_ci_input_t input;
  char name[CMDB_MAX_NAME];

  if (cmdb_str_copy(name, sizeof(name),
                    host->hostname[0] != '\0' ? host->hostname
                                              : host->ip_address) != 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "CI name is too long");
    return -1;
  }
  memset(&input, 0, sizeof(input));
  input.name = name;
  input.type_id = type_id;
  input.description = description;
  input.environment = environment;
  input.status = status;
  input.criticality = criticality;
  input.hosting_location = location;
  attrs[attr_count].key = "ip_address";
  attrs[attr_count++].value = host->ip_address;
  if (host->hostname[0] != '\0') {
    attrs[attr_count].key = "hostname";
    attrs[attr_count++].value = host->hostname;
  }
  if (host->mac_address[0] != '\0') {
    attrs[attr_count].key = "mac_address";
    attrs[attr_count++].value = host->mac_address;
  }
  if (host->vendor[0] != '\0') {
    attrs[attr_count].key = "vendor";
    attrs[attr_count++].value = host->vendor;
  }
  if (host->os_guess[0] != '\0') {
    attrs[attr_count].key = "os_guess";
    attrs[attr_count++].value = host->os_guess;
  }
  if (host->services[0] != '\0') {
    attrs[attr_count].key = "services";
    attrs[attr_count++].value = host->services;
  }
  if (host->open_ports[0] != '\0') {
    attrs[attr_count].key = "open_ports";
    attrs[attr_count++].value = host->open_ports;
  }
  attrs[attr_count].key = "discovery_method";
  attrs[attr_count++].value = "nmap";
  if (target != NULL && target[0] != '\0') {
    attrs[attr_count].key = "discovery_target";
    attrs[attr_count++].value = target;
  }
  input.attrs = attrs;
  input.attr_count = attr_count;
  return cmdb_ci_create(app->db, &input, id, err);
}

static int scan_merge_attr(cmdb_attr_input_t *attrs, size_t *count,
                           const char *key, const char *value, int overwrite) {
  size_t i;

  if (value == NULL || value[0] == '\0') {
    return 0;
  }
  for (i = 0; i < *count; i++) {
    if (strcmp(attrs[i].key, key) == 0) {
      if (overwrite || attrs[i].value == NULL || attrs[i].value[0] == '\0') {
        attrs[i].value = value;
      }
      return 0;
    }
  }
  attrs[*count].key = key;
  attrs[*count].value = value;
  (*count)++;
  return 0;
}

static int scan_merge_into_ci(cmdb_app_t *app, cmdb_scan_host_t *host,
                              const char *ci_id, const char *target,
                              int overwrite, const char *description,
                              const char *environment, const char *status,
                              const char *criticality, const char *location,
                              cmdb_error_t *err) {
  cmdb_ci_t ci;
  cmdb_ci_input_t input;
  cmdb_attr_input_t *attrs = NULL;
  size_t attr_count = 0;
  size_t i;
  int rc = -1;

  if (cmdb_ci_get(app->db, ci_id, &ci, err) != 0) {
    return -1;
  }
  attrs = cmdb_calloc(ci.attr_count + 10, sizeof(*attrs));
  for (i = 0; i < ci.attr_count; i++) {
    attrs[attr_count].key = ci.attrs[i].key;
    attrs[attr_count].value = ci.attrs[i].value;
    attr_count++;
  }
  {
    char value[CMDB_SCAN_OPEN_PORTS_SIZE];
    if (scan_host_attr(host, "ip_address", value, sizeof(value)) == 0) {
      (void)scan_merge_attr(attrs, &attr_count, "ip_address", host->ip_address,
                            overwrite);
    }
    if (scan_host_attr(host, "hostname", value, sizeof(value)) == 0) {
      (void)scan_merge_attr(attrs, &attr_count, "hostname", host->hostname,
                            overwrite);
    }
    if (scan_host_attr(host, "mac_address", value, sizeof(value)) == 0) {
      (void)scan_merge_attr(attrs, &attr_count, "mac_address",
                            host->mac_address, overwrite);
    }
    if (scan_host_attr(host, "vendor", value, sizeof(value)) == 0) {
      (void)scan_merge_attr(attrs, &attr_count, "vendor", host->vendor,
                            overwrite);
    }
    if (scan_host_attr(host, "os_guess", value, sizeof(value)) == 0) {
      (void)scan_merge_attr(attrs, &attr_count, "os_guess", host->os_guess,
                            overwrite);
    }
    if (scan_host_attr(host, "services", value, sizeof(value)) == 0) {
      (void)scan_merge_attr(attrs, &attr_count, "services", host->services,
                            overwrite);
    }
    if (scan_host_attr(host, "open_ports", value, sizeof(value)) == 0) {
      (void)scan_merge_attr(attrs, &attr_count, "open_ports",
                            host->open_ports, overwrite);
    }
  }
  (void)scan_merge_attr(attrs, &attr_count, "discovery_method", "nmap", 1);
  if (target != NULL && target[0] != '\0') {
    (void)scan_merge_attr(attrs, &attr_count, "discovery_target", target, 0);
  }
  memset(&input, 0, sizeof(input));
  input.name = ci.name;
  input.type_id = ci.type_id;
  input.description = ci.description[0] != '\0' ? ci.description
                                                : description;
  input.environment = ci.environment[0] != '\0' ? ci.environment : environment;
  input.status = ci.status[0] != '\0' ? ci.status : status;
  input.business_service = ci.business_service;
  input.technical_owner = ci.technical_owner;
  input.support_team = ci.support_team;
  input.criticality = ci.criticality[0] != '\0' ? ci.criticality : criticality;
  input.hosting_location = ci.hosting_location[0] != '\0'
                             ? ci.hosting_location
                             : location;
  input.external_id = ci.external_id;
  input.attrs = attrs;
  input.attr_count = attr_count;
  if (cmdb_ci_update(app->db, ci.id, &input, err) == 0) {
    rc = 0;
  }
  free(attrs);
  cmdb_ci_free(&ci);
  return rc;
}

static cJSON *handle_scan_import(cmdb_app_t *app, cJSON *params,
                                 cmdb_error_t *err) {  const char *job_id = NULL;
  const char *type_id = NULL;
  const char *description = NULL;
  const char *environment = NULL;
  const char *status = NULL;
  const char *criticality = NULL;
  const char *location = NULL;
  cJSON *decisions;
  cJSON *items;
  cJSON *decision;
  cJSON *result;
  cmdb_scan_result_t scan;
  cmdb_ci_type_t type;
  size_t created = 0;
  size_t merged = 0;
  size_t ignored = 0;
  size_t failed = 0;
  int have_type = 0;

  if (require_params(params, err) != 0 ||
      get_string(params, "job_id", &job_id, 1, err) != 0 ||
      get_string(params, "type_id", &type_id, 0, err) != 0 ||
      get_string(params, "description", &description, 0, err) != 0 ||
      get_string(params, "environment", &environment, 0, err) != 0 ||
      get_string(params, "status", &status, 0, err) != 0 ||
      get_string(params, "criticality", &criticality, 0, err) != 0 ||
      get_string(params, "hosting_location", &location, 0, err) != 0) {
    return NULL;
  }
  decisions = cJSON_GetObjectItemCaseSensitive(params, "decisions");
  if (!cJSON_IsArray(decisions) || cJSON_GetArraySize(decisions) == 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Provide at least one discovery decision");
    return NULL;
  }
  if (cJSON_GetArraySize(decisions) > (int)CMDB_SCAN_MAX_HOSTS) {
    cmdb_error_set(err, CMDB_ERR_LIMIT, "Too many discovery decisions");
    return NULL;
  }
  if (type_id != NULL && type_id[0] != '\0') {
    if (cmdb_types_get(app->db, type_id, &type, err) != 0) {
      return NULL;
    }
    have_type = 1;
  }
  if (description == NULL || description[0] == '\0') {
    description = "Discovered by nmap network scan";
  }
  if (environment == NULL) {
    environment = "";
  }
  if (status == NULL) {
    status = "";
  }
  if (criticality == NULL) {
    criticality = "";
  }
  if (location == NULL) {
    location = "";
  }
  memset(&scan, 0, sizeof(scan));
  if (cmdb_scan_store_load(app->db, job_id, &scan, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  items = cJSON_CreateArray();
  if (result == NULL || items == NULL) {
    cJSON_Delete(result);
    cJSON_Delete(items);
    cmdb_scan_result_free(&scan);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create import result");
    return NULL;
  }
  cJSON_ArrayForEach(decision, decisions) {
    cJSON *item = cJSON_CreateObject();
    cmdb_error_t item_err;
    const char *asset_id = NULL;
    const char *action = NULL;
    const char *ci_id = NULL;
    int overwrite = 0;
    cmdb_scan_host_t *host = NULL;
    size_t i;

    if (item == NULL) {
      break;
    }
    cmdb_error_clear(&item_err);
    if (get_string(decision, "asset_id", &asset_id, 1, &item_err) != 0 ||
        get_string(decision, "action", &action, 1, &item_err) != 0 ||
        get_string(decision, "ci_id", &ci_id, 0, &item_err) != 0 ||
        get_bool(decision, "overwrite", 0, &overwrite, &item_err) != 0) {
      failed++;
      cJSON_AddBoolToObject(item, "ok", 0);
      cJSON_AddStringToObject(item, "error", item_err.message);
      cJSON_AddItemToArray(items, item);
      continue;
    }
    for (i = 0; i < scan.host_count; i++) {
      if (strcmp(scan.hosts[i].asset_id, asset_id) == 0) {
        host = &scan.hosts[i];
        break;
      }
    }
    if (host == NULL) {
      failed++;
      cJSON_AddBoolToObject(item, "ok", 0);
      cJSON_AddStringToObject(item, "error", "Discovered host not found");
      cJSON_AddItemToArray(items, item);
      continue;
    }
    cJSON_AddStringToObject(item, "asset_id", host->asset_id);
    cJSON_AddStringToObject(item, "ip_address", host->ip_address);
    cJSON_AddStringToObject(item, "action", action);
    if (strcmp(action, "ignore") == 0) {
      if (cmdb_scan_asset_set_review(app->db, host->asset_id, "ignored", "",
                                     &item_err) == 0) {
        ignored++;
        cJSON_AddBoolToObject(item, "ok", 1);
      } else {
        failed++;
        cJSON_AddBoolToObject(item, "ok", 0);
        cJSON_AddStringToObject(item, "error", item_err.message);
      }
    } else if (strcmp(action, "create") == 0) {
      char new_id[37];
      if (!have_type) {
        item_err.code = CMDB_ERR_VALIDATION;
        (void)cmdb_str_copy(item_err.message, sizeof(item_err.message),
                            "Choose a CI type for new records");
        item_err.detail[0] = '\0';
      }
      if (item_err.code == CMDB_OK &&
          scan_create_from_host(app, type_id, scan.target, host, description,
                                environment, status, criticality, location,
                                new_id, &item_err) == 0 &&
          cmdb_scan_asset_set_review(app->db, host->asset_id, "created",
                                     new_id, &item_err) == 0) {
        created++;
        cJSON_AddBoolToObject(item, "ok", 1);
        cJSON_AddStringToObject(item, "ci_id", new_id);
      } else {
        failed++;
        cJSON_AddBoolToObject(item, "ok", 0);
        cJSON_AddStringToObject(item, "error",
                                item_err.message[0] != '\0'
                                  ? item_err.message
                                  : "Import failed");
      }
    } else if (strcmp(action, "merge") == 0) {
      if (ci_id == NULL || ci_id[0] == '\0') {
        item_err.code = CMDB_ERR_VALIDATION;
        (void)cmdb_str_copy(item_err.message, sizeof(item_err.message),
                            "Choose an existing CI to merge into");
      }
      if (item_err.code == CMDB_OK &&
          scan_merge_into_ci(app, host, ci_id, scan.target, overwrite,
                             description, environment, status, criticality,
                             location, &item_err) == 0 &&
          cmdb_scan_asset_set_review(app->db, host->asset_id, "merged",
                                     ci_id, &item_err) == 0) {
        merged++;
        cJSON_AddBoolToObject(item, "ok", 1);
        cJSON_AddStringToObject(item, "ci_id", ci_id);
      } else {
        failed++;
        cJSON_AddBoolToObject(item, "ok", 0);
        cJSON_AddStringToObject(item, "error",
                                item_err.message[0] != '\0'
                                  ? item_err.message
                                  : "Merge failed");
      }
    } else {
      failed++;
      cJSON_AddBoolToObject(item, "ok", 0);
      cJSON_AddStringToObject(item, "error",
                              "Action must be create, merge, or ignore");
    }
    cJSON_AddItemToArray(items, item);
  }
  cJSON_AddNumberToObject(result, "created", (double)created);
  cJSON_AddNumberToObject(result, "merged", (double)merged);
  cJSON_AddNumberToObject(result, "ignored", (double)ignored);
  cJSON_AddNumberToObject(result, "failed", (double)failed);
  cJSON_AddItemToObject(result, "items", items);
  {
    cJSON *detail = cJSON_CreateObject();
    char *json = NULL;
    if (detail != NULL) {
      cJSON_AddStringToObject(detail, "job_id", job_id);
      cJSON_AddNumberToObject(detail, "created", (double)created);
      cJSON_AddNumberToObject(detail, "merged", (double)merged);
      cJSON_AddNumberToObject(detail, "ignored", (double)ignored);
      cJSON_AddNumberToObject(detail, "failed", (double)failed);
      json = cJSON_PrintUnformatted(detail);
      cJSON_Delete(detail);
    }
    if (json != NULL) {
      (void)cmdb_audit_add(app->db, cmdb_audit_actor(), "import", "network_scan",
                           job_id, json, NULL);
      free(json);
    }
  }
  cmdb_scan_result_free(&scan);
  return result;
}

static cJSON *diagram_to_json(const cmdb_diagram_t *diagram,
                              int include_xml) {
  cJSON *obj = cJSON_CreateObject();
  if (obj == NULL) {
    return NULL;
  }
  cJSON_AddStringToObject(obj, "id", diagram->id);
  cJSON_AddStringToObject(obj, "name", diagram->name);
  cJSON_AddStringToObject(obj, "created_at", diagram->created_at);
  cJSON_AddStringToObject(obj, "updated_at", diagram->updated_at);
  if (include_xml) {
    cJSON_AddStringToObject(obj, "xml",
                            diagram->drawio_xml != NULL
                              ? diagram->drawio_xml
                              : "");
  }
  return obj;
}

static cJSON *handle_diagrams_list(cmdb_app_t *app, cmdb_error_t *err) {
  cmdb_diagram_t *diagrams = NULL;
  size_t count = 0;
  size_t i;
  cJSON *result = cJSON_CreateObject();
  cJSON *items = cJSON_CreateArray();

  if (result == NULL || items == NULL) {
    cJSON_Delete(result);
    cJSON_Delete(items);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create diagram list");
    return NULL;
  }
  if (cmdb_diagram_list(app->db, &diagrams, &count, 0, err) != 0) {
    cJSON_Delete(result);
    cJSON_Delete(items);
    return NULL;
  }
  for (i = 0; i < count; i++) {
    cJSON *item = diagram_to_json(&diagrams[i], 0);
    if (item != NULL) {
      cJSON_AddItemToArray(items, item);
    }
  }
  cmdb_diagram_free_list(diagrams, count);
  cJSON_AddItemToObject(result, "items", items);
  return result;
}

static cJSON *handle_diagrams_get(cmdb_app_t *app, cJSON *params,
                                  cmdb_error_t *err) {
  const char *id = NULL;
  cmdb_diagram_t diagram;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_diagram_get(app->db, id, &diagram, err) != 0) {
    return NULL;
  }
  result = diagram_to_json(&diagram, 1);
  cmdb_diagram_free(&diagram);
  if (result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create diagram");
  }
  return result;
}

static cJSON *handle_diagrams_save(cmdb_app_t *app, cJSON *params,
                                   cmdb_error_t *err) {
  const char *id = NULL;
  const char *name = NULL;
  const char *xml = NULL;
  char out_id[37];
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 0, err) != 0 ||
      get_string(params, "name", &name, 1, err) != 0 ||
      get_string(params, "xml", &xml, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_diagram_save(app->db, id, name, xml, out_id, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddStringToObject(result, "id", out_id);
  }
  return result;
}

static cJSON *handle_diagrams_delete(cmdb_app_t *app, cJSON *params,
                                     cmdb_error_t *err) {
  const char *id = NULL;
  cJSON *result;

  if (require_params(params, err) != 0 ||
      get_string(params, "id", &id, 1, err) != 0) {
    return NULL;
  }
  if (cmdb_diagram_delete(app->db, id, err) != 0) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if (result != NULL) {
    cJSON_AddBoolToObject(result, "deleted", 1);
  }
  return result;
}

cJSON *cmdb_api_handle(cmdb_app_t *app, cJSON *request) {
  cJSON *params;
  cJSON *id;
  const char *method = NULL;
  cJSON *result = NULL;
  cmdb_error_t err;

  cmdb_error_clear(&err);
  id = cJSON_GetObjectItemCaseSensitive(request, "id");
  params = cmdb_json_get_params(request);
  if (cmdb_json_string(request, "method", &method, 1, &err) != 0) {
    return api_error_response(id, &err);
  }

  if (strcmp(method, "app.info") == 0) {
    result = handle_app_info(app);
  } else if (strcmp(method, "prefs.get") == 0) {
    result = handle_prefs_get(app, &err);
  } else if (strcmp(method, "prefs.set") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_prefs_set(app, params, &err);
    }
  } else if (strcmp(method, "settings.get") == 0) {
    result = handle_settings_get(&err);
  } else if (strcmp(method, "settings.remember") == 0) {
    result = handle_settings_remember(params, &err);
  } else if (strcmp(method, "db.create") == 0) {
    result = handle_db_create(app, params, &err);
  } else if (strcmp(method, "db.open") == 0) {
    result = handle_db_open(app, params, &err);
  } else if (strcmp(method, "db.lock") == 0) {
    result = handle_db_lock(app, &err);
  } else if (strcmp(method, "db.status") == 0) {
    result = handle_db_status(app);
  } else if (strcmp(method, "db.backup") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_db_backup(app, params, &err);
    }
  } else if (strcmp(method, "db.restore") == 0) {
    result = handle_db_restore(params, &err);
  } else if (strcmp(method, "dialog.choose_database") == 0) {
    result = handle_choose_database(app, params, &err);
  } else if (strcmp(method, "types.list") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_types_list(app, &err);
    }
  } else if (strcmp(method, "categories.list") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_categories_list(app, &err);
    }
  } else if (strcmp(method, "categories.create") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_categories_create(app, params, &err);
    }
  } else if (strcmp(method, "categories.update") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_categories_update(app, params, &err);
    }
  } else if (strcmp(method, "categories.delete") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_categories_delete(app, params, &err);
    }
  } else if (strcmp(method, "types.get") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_types_get(app, params, &err);
    }
  } else if (strcmp(method, "types.create") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_types_create(app, params, &err);
    }
  } else if (strcmp(method, "types.update") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_types_update(app, params, &err);
    }
  } else if (strcmp(method, "types.delete") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_types_delete(app, params, &err);
    }
  } else if (strcmp(method, "types.field_add") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_field_add(app, params, &err);
    }
  } else if (strcmp(method, "types.field_update") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_field_update(app, params, &err);
    }
  } else if (strcmp(method, "types.field_delete") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_field_delete(app, params, &err);
    }
  } else if (strcmp(method, "cis.list") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_cis_list(app, params, &err);
    }
  } else if (strcmp(method, "cis.get") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_cis_get(app, params, &err);
    }
  } else if (strcmp(method, "cis.create") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_cis_create(app, params, &err);
    }
  } else if (strcmp(method, "cis.update") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_cis_update(app, params, &err);
    }
  } else if (strcmp(method, "cis.delete") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_cis_delete(app, params, &err);
    }
  } else if (strcmp(method, "relationships.types") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_rel_types(app, &err);
    }
  } else if (strcmp(method, "relationships.list") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_rel_list(app, params, &err);
    }
  } else if (strcmp(method, "relationships.create") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_rel_create(app, params, &err);
    }
  } else if (strcmp(method, "relationships.set_impact") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_rel_set_impact(app, params, &err);
    }
  } else if (strcmp(method, "relationships.delete") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_rel_delete(app, params, &err);
    }
  } else if (strcmp(method, "import.preview") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_import(app, params, 0, &err);
    }
  } else if (strcmp(method, "import.commit") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_import(app, params, 1, &err);
    }
  } else if (strcmp(method, "graph.dependencies") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_graph_dependencies(app, params, &err);
    }
  } else if (strcmp(method, "graph.save_drawio") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_graph_save_drawio(app, params, &err);
    }
  } else if (strcmp(method, "scan.start") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_scan_start(app, params, &err);
    }
  } else if (strcmp(method, "scan.status") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_scan_status(app, params, &err);
    }
  } else if (strcmp(method, "scan.cancel") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_scan_cancel(app, params, &err);
    }
  } else if (strcmp(method, "scan.jobs") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_scan_jobs(app, &err);
    }
  } else if (strcmp(method, "scan.job") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_scan_job(app, params, &err);
    }
  } else if (strcmp(method, "scan.discard") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_scan_discard(app, params, &err);
    }
  } else if (strcmp(method, "scan.import") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_scan_import(app, params, &err);
    }
  } else if (strcmp(method, "export.save") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_export_save(app, params, &err);
    }
  } else if (strcmp(method, "diagrams.list") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_diagrams_list(app, &err);
    }
  } else if (strcmp(method, "diagrams.get") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_diagrams_get(app, params, &err);
    }
  } else if (strcmp(method, "diagrams.save") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_diagrams_save(app, params, &err);
    }
  } else if (strcmp(method, "diagrams.delete") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_diagrams_delete(app, params, &err);
    }
  } else if (strcmp(method, "auth.state.get") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_auth_state_get(app, &err);
    }
  } else if (strcmp(method, "auth.state.set") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_auth_state_set(app, params, &err);
    }
  } else if (strcmp(method, "auth.audit") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_auth_audit(app, params, &err);
    }
  } else if (strcmp(method, "security.info") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_security_info(app, &err);
    }
  } else if (strcmp(method, "audit.verify") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_audit_verify(app, &err);
    }
  } else if (strcmp(method, "audit.list") == 0) {
    if (require_db(app, &err) == 0) {
      result = handle_audit_list(app, params, &err);
    }
  } else {
    cmdb_error_set(&err, CMDB_ERR_NOT_FOUND, "Unknown API method");
    cmdb_error_set_detail(&err, "%s", method);
  }

  if (result == NULL) {
    if (err.code == CMDB_OK) {
      cmdb_error_set(&err, CMDB_ERR_INTERNAL, "API method failed");
    }
    return api_error_response(id, &err);
  }
  return api_ok_response(request, result);
}
