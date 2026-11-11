#include "cmdb/export.h"

#include "cmdb/ci.h"
#include "cmdb/relationships.h"
#include "cmdb/util.h"

#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
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

static int export_text_equal(const char *a, const char *b) {
  int ca;
  int cb;

  if (a == NULL || b == NULL) {
    return a == b;
  }
  while (*a != '\0' && *b != '\0') {
    ca = tolower((unsigned char)*a);
    cb = tolower((unsigned char)*b);
    if (ca != cb) {
      return 0;
    }
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

static int export_text_contains(const char *haystack, const char *needle) {
  size_t needle_len;

  if (haystack == NULL || needle == NULL || needle[0] == '\0') {
    return 0;
  }
  needle_len = strlen(needle);
  while (*haystack != '\0') {
    size_t i;
    for (i = 0; i < needle_len; i++) {
      if (haystack[i] == '\0' ||
          tolower((unsigned char)haystack[i]) !=
            tolower((unsigned char)needle[i])) {
        break;
      }
    }
    if (i == needle_len) {
      return 1;
    }
    haystack++;
  }
  return 0;
}

static int export_host_like(const char *value) {
  const unsigned char *p;

  if (value == NULL || value[0] == '\0') {
    return 0;
  }
  for (p = (const unsigned char *)value; *p != '\0'; p++) {
    if (!(isalnum(*p) || *p == '.' || *p == '-' || *p == '_' ||
          *p == ':' || *p == '%')) {
      return 0;
    }
  }
  return 1;
}

static const char *export_first_attr(const cmdb_ci_t *ci, const char *key,
                                     const char *fallback) {
  const char *value = export_attr_value(ci, key);
  return value[0] != '\0' ? value : fallback;
}

static const char *hibp_address(const cmdb_ci_t *ci) {
  const char *value;

  value = export_attr_value(ci, "ip_address");
  if (export_host_like(value)) {
    return value;
  }
  value = export_attr_value(ci, "management_ip");
  if (export_host_like(value)) {
    return value;
  }
  value = export_attr_value(ci, "hostname");
  if (export_host_like(value)) {
    return value;
  }
  value = export_attr_value(ci, "address");
  if (export_host_like(value)) {
    return value;
  }
  return export_host_like(ci->name) ? ci->name : NULL;
}

static int hibp_linux_ci(const cmdb_ci_t *ci) {
  const char *family = export_attr_value(ci, "os_family");
  return (family[0] != '\0' && export_text_equal(family, "linux")) ||
         export_text_contains(ci->type_name, "linux");
}

static const char *hibp_auth_type(const char *value) {
  if (export_text_equal(value, "key")) {
    return "key";
  }
  if (export_text_equal(value, "agent")) {
    return "agent";
  }
  if (export_text_equal(value, "certificate")) {
    return "certificate";
  }
  if (export_text_equal(value, "password")) {
    return "password";
  }
  return NULL;
}

static int hibp_port(const cmdb_ci_t *ci, int default_port, int *out) {
  const char *value = export_attr_value(ci, "ssh_port");
  if (value[0] == '\0') {
    value = export_attr_value(ci, "port");
  }
  if (value[0] == '\0') {
    *out = default_port;
    return 0;
  }
  return cmdb_parse_int(value, 1, 65535, out);
}

static void hibp_append_tag(cmdb_strbuf_t *tags, const char *value) {
  if (value == NULL || value[0] == '\0') {
    return;
  }
  if (tags->len > 0) {
    cmdb_strbuf_append(tags, ",");
  }
  cmdb_strbuf_append(tags, value);
}

static int export_hibp_csv(cmdb_db_t *db, const char *path,
                           const cmdb_hibp_export_options_t *options,
                           cmdb_hibp_export_stats_t *stats,
                           cmdb_error_t *err) {
  cmdb_strbuf_t buffer;
  cmdb_ci_filter_t filter;
  size_t offset = 0;
  size_t exported = 0;
  size_t skipped = 0;

  if (stats != NULL) {
    stats->exported = 0;
    stats->skipped = 0;
  }
  if (options == NULL || options->ssh_username == NULL ||
      options->ssh_username[0] == '\0') {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "A default SSH username is required");
    return -1;
  }
  if (options->ssh_port < 1 || options->ssh_port > 65535) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "The default SSH port must be between 1 and 65535");
    return -1;
  }
  if (hibp_auth_type(options->ssh_auth_type == NULL ? "agent"
                                                    : options->ssh_auth_type)
        == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "The default SSH authentication type is invalid");
    return -1;
  }

  cmdb_strbuf_init(&buffer);
  cmdb_strbuf_append(
    &buffer,
    "name,address,port,username,auth_type,key_path,group,tags,"
    "cmdb_ci_id,external_id,os_family,os_version,status\n");

  memset(&filter, 0, sizeof(filter));
  filter.limit = CMDB_MAX_LIST_LIMIT;
  for (;;) {
    cmdb_ci_t *items = NULL;
    size_t count = 0;
    size_t total = 0;
    size_t i;

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
      cmdb_strbuf_t tags;
      const char *address;
      const char *username;
      const char *auth_value;
      const char *auth_type;
      const char *key_path;
      const char *group;
      const char *os_version;
      int port;
      char port_text[16];

      if (cmdb_ci_get(db, items[i].id, &ci, err) != 0) {
        cmdb_ci_free_list(items, count);
        cmdb_strbuf_free(&buffer);
        return -1;
      }
      if (!hibp_linux_ci(&ci)) {
        cmdb_ci_free(&ci);
        continue;
      }
      address = hibp_address(&ci);
      if (address == NULL || hibp_port(&ci, options->ssh_port, &port) != 0) {
        skipped++;
        cmdb_ci_free(&ci);
        continue;
      }
      auth_value = export_first_attr(&ci, "ssh_auth_type",
                                     export_first_attr(&ci, "auth_type",
                                                       options->ssh_auth_type));
      auth_type = hibp_auth_type(auth_value == NULL ? "agent" : auth_value);
      if (auth_type == NULL) {
        skipped++;
        cmdb_ci_free(&ci);
        continue;
      }
      username = export_first_attr(&ci, "ssh_username",
                                   export_first_attr(&ci, "username",
                                                     options->ssh_username));
      key_path = export_first_attr(&ci, "ssh_key_path",
                                   export_first_attr(&ci, "key_path",
                                     options->ssh_key_path == NULL
                                       ? "" : options->ssh_key_path));
      if (strcmp(auth_type, "key") == 0 && key_path[0] == '\0') {
        skipped++;
        cmdb_ci_free(&ci);
        continue;
      }
      group = ci.environment[0] != '\0'
                ? ci.environment
                : (ci.support_team[0] != '\0'
                     ? ci.support_team
                     : ci.hosting_location);
      os_version = export_first_attr(&ci, "os_version",
                                     export_first_attr(&ci, "os", ""));

      cmdb_strbuf_init(&tags);
      hibp_append_tag(&tags, "cmdb-lite");
      hibp_append_tag(&tags, ci.environment);
      hibp_append_tag(&tags, ci.criticality);
      hibp_append_tag(&tags, ci.support_team);
      hibp_append_tag(&tags, ci.status);

      (void)snprintf(port_text, sizeof(port_text), "%d", port);
      export_csv_value(&buffer, ci.name);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, address);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, port_text);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, username);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, auth_type);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, key_path);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, group);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, tags.data != NULL ? tags.data : "");
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.id);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.external_id);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, "Linux");
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, os_version);
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, ci.status);
      cmdb_strbuf_append(&buffer, "\n");

      cmdb_strbuf_free(&tags);
      exported++;
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
    if (rc == 0 && stats != NULL) {
      stats->exported = exported;
      stats->skipped = skipped;
    }
    return rc;
  }
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
                     "source_ci,relationship_type,target_ci,description,"
                     "impact\n");
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
      export_csv_end(&buffer, 0);
      export_csv_value(&buffer, items[i].impact);
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
      cJSON_AddStringToObject(item, "impact", items[i].impact);
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

int cmdb_export_hibp_csv(cmdb_db_t *db, const char *path,
                         const cmdb_hibp_export_options_t *options,
                         cmdb_hibp_export_stats_t *stats,
                         cmdb_error_t *err) {
  if (db == NULL || path == NULL || path[0] == '\0') {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid export arguments");
    return -1;
  }
  return export_hibp_csv(db, path, options, stats, err);
}
