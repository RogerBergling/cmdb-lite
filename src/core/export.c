#include "cmdb/export.h"

#include "cmdb/ci.h"
#include "cmdb/relationships.h"
#include "cmdb/util.h"

#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

static void export_csv_field(cmdb_strbuf_t *buffer, const char *value) {
  const char *cursor;
  int quote = 0;

  if (value == NULL) {
    value = "";
  }
  for (cursor = value; *cursor != '\0'; cursor++) {
    if (*cursor == ',' || *cursor == '"' || *cursor == '\n' ||
        *cursor == '\r') {
      quote = 1;
      break;
    }
  }
  if (quote) {
    cmdb_strbuf_append(buffer, "\"");
  }
  for (cursor = value; *cursor != '\0'; cursor++) {
    if (*cursor == '"') {
      cmdb_strbuf_append(buffer, "\"\"");
    } else {
      cmdb_strbuf_append_n(buffer, cursor, 1);
    }
  }
  if (quote) {
    cmdb_strbuf_append(buffer, "\"");
  }
}

static void export_csv_value(cmdb_strbuf_t *buffer, const char *value) {
  export_csv_field(buffer, value);
}

static void export_csv_end(cmdb_strbuf_t *buffer, int last) {
  if (!last) {
    cmdb_strbuf_append(buffer, ",");
  }
}

static int export_collect_attribute_keys(cmdb_db_t *db,
                                         char keys[][CMDB_MAX_FIELD_KEY],
                                         size_t *count, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  *count = 0;
  if (sqlite3_prepare_v2(
        cmdb_db_sqlite(db),
        "SELECT DISTINCT field_key FROM ci_attributes "
        "ORDER BY field_key;",
        -1, &stmt, NULL) != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list export attributes");
    return -1;
  }
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW &&
         *count < CMDB_MAX_ATTRIBUTES) {
    const char *key = (const char *)sqlite3_column_text(stmt, 0);
    (void)cmdb_str_copy(keys[*count], CMDB_MAX_FIELD_KEY, key);
    (*count)++;
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot list export attributes");
    return -1;
  }
  return 0;
}

static const char *export_attr_value(const cmdb_ci_t *ci, const char *key) {
  size_t i;
  for (i = 0; i < ci->attr_count; i++) {
    if (strcmp(ci->attrs[i].key, key) == 0) {
      return ci->attrs[i].value;
    }
  }
  return "";
}

static int export_cis_csv(cmdb_db_t *db, const char *path,
                          cmdb_error_t *err) {
  static char keys[CMDB_MAX_ATTRIBUTES][CMDB_MAX_FIELD_KEY];
  size_t key_count = 0;
  size_t offset = 0;
  cmdb_strbuf_t buffer;
  cmdb_ci_filter_t filter;
  size_t i;
  size_t k;

  if (export_collect_attribute_keys(db, keys, &key_count, err) != 0) {
    return -1;
  }
  cmdb_strbuf_init(&buffer);
  cmdb_strbuf_append(
    &buffer,
    "ci_id,ci_name,ci_type,description,environment,status,"
    "business_service,technical_owner,support_team,criticality,"
    "hosting_location,last_updated,external_id");
  for (k = 0; k < key_count; k++) {
    cmdb_strbuf_append(&buffer, ",custom.");
    export_csv_value(&buffer, keys[k]);
  }
  cmdb_strbuf_append(&buffer, "\n");

  memset(&filter, 0, sizeof(filter));
  filter.limit = CMDB_MAX_LIST_LIMIT;
  for (;;) {
    cmdb_ci_t *items = NULL;
    size_t count = 0;
    size_t total = 0;
    filter.offset = offset;
    if (cmdb_ci_list(db, &filter, &items, &count, &total, err) != 0) {
      cmdb_strbuf_free(&buffer);
      return -1;
    }
    if (count == 0) {
      cmdb_ci_free_list(items, count);
      break;
    }
    for (i = 0; i < count; i++) {
      cmdb_ci_t ci;
      if (cmdb_ci_get(db, items[i].id, &ci, err) != 0) {
        cmdb_ci_free_list(items, count);
        cmdb_strbuf_free(&buffer);
        return -1;
      }
      export_csv_value(&buffer, ci.id);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.name);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.type_name);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.description);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.environment);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.status);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.business_service);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.technical_owner);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.support_team);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.criticality);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.hosting_location);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.updated_at);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.external_id);
      for (k = 0; k < key_count; k++) {
        cmdb_strbuf_append(&buffer, ",");
        export_csv_value(&buffer, export_attr_value(&ci, keys[k]));
      }
      cmdb_strbuf_append(&buffer, "\n");
      cmdb_ci_free(&ci);
    }
    cmdb_ci_free_list(items, count);
    offset += count;
    if (offset >= total) {
      break;
    }
  }
  {
    char *data = cmdb_strbuf_steal(&buffer);
    int rc;
    if (data == NULL) {
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot build CSV export");
      return -1;
    }
    rc = cmdb_file_write_atomic(path, data, strlen(data), 0600, err);
    free(data);
    return rc;
  }
}

static int export_relationships_csv(cmdb_db_t *db, const char *path,
                                   cmdb_error_t *err) {
  cmdb_strbuf_t buffer;
  size_t offset = 0;

  cmdb_strbuf_init(&buffer);
  cmdb_strbuf_append(&buffer,
                     "source_ci,relationship_type,target_ci,description\n");
  for (;;) {
    cmdb_relationship_t *items = NULL;
    size_t count = 0;
    size_t total = 0;
    size_t i;
    if (cmdb_relationship_list(db, NULL, CMDB_MAX_LIST_LIMIT, offset,
                               &items, &count, &total, err) != 0) {
      cmdb_strbuf_free(&buffer);
      return -1;
    }
    if (count == 0) {
      cmdb_relationship_free_list(items, count);
      break;
    }
    for (i = 0; i < count; i++) {
      export_csv_value(&buffer, items[i].source_name);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, items[i].type_name);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, items[i].target_name);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, items[i].description);
      cmdb_strbuf_append(&buffer, "\n");
    }
    cmdb_relationship_free_list(items, count);
    offset += count;
    if (offset >= total) {
      break;
    }
  }
  {
    char *data = cmdb_strbuf_steal(&buffer);
    int rc;
    if (data == NULL) {
      cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot build CSV export");
      return -1;
    }
    rc = cmdb_file_write_atomic(path, data, strlen(data), 0600, err);
    free(data);
    return rc;
  }
}

static cJSON *export_ci_json(const cmdb_ci_t *ci) {
  cJSON *obj = cJSON_CreateObject();
  cJSON *attrs = cJSON_CreateObject();
  size_t i;

  if (obj == NULL || attrs == NULL) {
    cJSON_Delete(obj);
    cJSON_Delete(attrs);
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
  for (i = 0; i < ci->attr_count; i++) {
    cJSON_AddStringToObject(attrs, ci->attrs[i].key, ci->attrs[i].value);
  }
  cJSON_AddItemToObject(obj, "attributes", attrs);
  return obj;
}

static int export_json(cmdb_db_t *db, const char *path, cmdb_error_t *err) {
  cJSON *root = cJSON_CreateObject();
  cJSON *cis = cJSON_CreateArray();
  cJSON *rels = cJSON_CreateArray();
  size_t offset = 0;
  char now[21];
  char *json;
  int rc;

  if (root == NULL || cis == NULL || rels == NULL) {
    cJSON_Delete(root);
    cJSON_Delete(cis);
    cJSON_Delete(rels);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot build JSON export");
    return -1;
  }
  cmdb_iso8601_now(now);
  cJSON_AddNumberToObject(root, "format_version", 1);
  cJSON_AddStringToObject(root, "exported_at", now);
  for (;;) {
    cmdb_ci_t *items = NULL;
    size_t count = 0;
    size_t total = 0;
    size_t i;
    cmdb_ci_filter_t filter;
    memset(&filter, 0, sizeof(filter));
    filter.limit = CMDB_MAX_LIST_LIMIT;
    filter.offset = offset;
    if (cmdb_ci_list(db, &filter, &items, &count, &total, err) != 0) {
      goto fail;
    }
    if (count == 0) {
      cmdb_ci_free_list(items, count);
      break;
    }
    for (i = 0; i < count; i++) {
      cmdb_ci_t ci;
      cJSON *item;
      if (cmdb_ci_get(db, items[i].id, &ci, err) != 0) {
        cmdb_ci_free_list(items, count);
        goto fail;
      }
      item = export_ci_json(&ci);
      cmdb_ci_free(&ci);
      if (item == NULL) {
        cmdb_ci_free_list(items, count);
        cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot build JSON export");
        goto fail;
      }
      cJSON_AddItemToArray(cis, item);
    }
    cmdb_ci_free_list(items, count);
    offset += count;
    if (offset >= total) {
      break;
    }
  }
  offset = 0;
  for (;;) {
    cmdb_relationship_t *items = NULL;
    size_t count = 0;
    size_t total = 0;
    size_t i;
    if (cmdb_relationship_list(db, NULL, CMDB_MAX_LIST_LIMIT, offset,
                               &items, &count, &total, err) != 0) {
      goto fail;
    }
    if (count == 0) {
      cmdb_relationship_free_list(items, count);
      break;
    }
    for (i = 0; i < count; i++) {
      cJSON *item = cJSON_CreateObject();
      if (item == NULL) {
        cmdb_relationship_free_list(items, count);
        cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot build JSON export");
        goto fail;
      }
      cJSON_AddStringToObject(item, "id", items[i].id);
      cJSON_AddStringToObject(item, "source_id", items[i].source_id);
      cJSON_AddStringToObject(item, "source_name", items[i].source_name);
      cJSON_AddStringToObject(item, "target_id", items[i].target_id);
      cJSON_AddStringToObject(item, "target_name", items[i].target_name);
      cJSON_AddStringToObject(item, "type_id", items[i].type_id);
      cJSON_AddStringToObject(item, "type_name", items[i].type_name);
      cJSON_AddStringToObject(item, "description", items[i].description);
      cJSON_AddItemToArray(rels, item);
    }
    cmdb_relationship_free_list(items, count);
    offset += count;
    if (offset >= total) {
      break;
    }
  }
  cJSON_AddItemToObject(root, "cis", cis);
  cJSON_AddItemToObject(root, "relationships", rels);
  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot encode JSON export");
    return -1;
  }
  rc = cmdb_file_write_atomic(path, json, strlen(json), 0600, err);
  free(json);
  return rc;

fail:
  cJSON_Delete(root);
  cJSON_Delete(cis);
  cJSON_Delete(rels);
  return -1;
}

int cmdb_export_save(cmdb_db_t *db, cmdb_export_kind_t kind,
                     const char *path, cmdb_error_t *err) {
  if (db == NULL || path == NULL || path[0] == '\0') {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid export arguments");
    return -1;
  }
  switch (kind) {
    case CMDB_EXPORT_CI_CSV:
      return export_cis_csv(db, path, err);
    case CMDB_EXPORT_REL_CSV:
      return export_relationships_csv(db, path, err);
    case CMDB_EXPORT_JSON:
      return export_json(db, path, err);
    default:
      cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid export format");
      return -1;
  }
}
