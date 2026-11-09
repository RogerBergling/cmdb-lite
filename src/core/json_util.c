#include "cmdb/json_util.h"

#include "cmdb/crypto.h"
#include "cmdb/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define cmdb_isfinite(value) _finite(value)
#else
#define cmdb_isfinite(value) isfinite(value)
#endif

cJSON *cmdb_json_get_params(cJSON *request) {
  cJSON *params;
  if (!cJSON_IsObject(request)) {
    return NULL;
  }
  params = cJSON_GetObjectItemCaseSensitive(request, "params");
  return cJSON_IsObject(params) ? params : NULL;
}

int cmdb_json_require_object(cJSON *item, const char *what,
                             cmdb_error_t *err) {
  if (!cJSON_IsObject(item)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "%s must be an object",
                   what != NULL ? what : "value");
    return -1;
  }
  return 0;
}

int cmdb_json_string(cJSON *object, const char *key, const char **out,
                     int required, cmdb_error_t *err) {
  cJSON *item;

  if (out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Invalid JSON output");
    return -1;
  }
  *out = NULL;
  if (!cJSON_IsObject(object)) {
    if (required) {
      cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Missing parameters");
      return -1;
    }
    return 0;
  }
  item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (item == NULL) {
    if (required) {
      cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                     "Missing required parameter '%s'", key);
      return -1;
    }
    return 0;
  }
  if (!cJSON_IsString(item) || item->valuestring == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Parameter '%s' must be a string", key);
    return -1;
  }
  *out = item->valuestring;
  return 0;
}

int cmdb_json_string_len(cJSON *object, const char *key, const char **out,
                         size_t *out_len, int required, cmdb_error_t *err) {
  const char *value = NULL;
  if (out_len == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Invalid JSON output");
    return -1;
  }
  if (cmdb_json_string(object, key, &value, required, err) != 0) {
    return -1;
  }
  *out = value;
  *out_len = value != NULL ? strlen(value) : 0;
  return 0;
}

int cmdb_json_int(cJSON *object, const char *key, int default_value,
                  int min_value, int max_value, int *out, cmdb_error_t *err) {
  cJSON *item;
  double value;

  if (out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Invalid JSON output");
    return -1;
  }
  *out = default_value;
  if (!cJSON_IsObject(object)) {
    return 0;
  }
  item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (item == NULL) {
    return 0;
  }
  if (!cJSON_IsNumber(item)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Parameter '%s' must be a number", key);
    return -1;
  }
  value = item->valuedouble;
  if (!cmdb_isfinite(value) || value < min_value || value > max_value ||
      value != (double)(int)value) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Parameter '%s' is out of range", key);
    return -1;
  }
  *out = (int)value;
  return 0;
}

int cmdb_json_bool(cJSON *object, const char *key, int default_value,
                   int *out, cmdb_error_t *err) {
  cJSON *item;

  if (out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Invalid JSON output");
    return -1;
  }
  *out = default_value;
  if (!cJSON_IsObject(object)) {
    return 0;
  }
  item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (item == NULL) {
    return 0;
  }
  if (cJSON_IsBool(item)) {
    *out = cJSON_IsTrue(item) ? 1 : 0;
    return 0;
  }
  if (cJSON_IsNumber(item)) {
    if (item->valuedouble == 0.0) {
      *out = 0;
      return 0;
    }
    if (item->valuedouble == 1.0) {
      *out = 1;
      return 0;
    }
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Parameter '%s' must be a boolean", key);
    return -1;
  }
  if (cJSON_IsString(item)) {
    int parsed = 0;
    if (cmdb_parse_bool(item->valuestring, &parsed) == 0) {
      *out = parsed;
      return 0;
    }
  }
  cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                 "Parameter '%s' must be a boolean", key);
  return -1;
}

int cmdb_json_string_array(cJSON *object, const char *key,
                           char ***out, size_t *out_count,
                           cmdb_error_t *err) {
  cJSON *item;
  size_t count;
  size_t i;
  char **items;

  if (out == NULL || out_count == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Invalid JSON output");
    return -1;
  }
  *out = NULL;
  *out_count = 0;
  if (!cJSON_IsObject(object)) {
    return 0;
  }
  item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (item == NULL) {
    return 0;
  }
  if (!cJSON_IsArray(item)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Parameter '%s' must be an array", key);
    return -1;
  }
  count = (size_t)cJSON_GetArraySize(item);
  if (count == 0) {
    return 0;
  }
  items = cmdb_calloc(count, sizeof(*items));
  for (i = 0; i < count; i++) {
    cJSON *entry = cJSON_GetArrayItem(item, (int)i);
    if (!cJSON_IsString(entry) || entry->valuestring == NULL) {
      cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                     "Parameter '%s' must contain strings", key);
      cmdb_json_free_string_array(items, count);
      return -1;
    }
    items[i] = cmdb_strdup(entry->valuestring);
  }
  *out = items;
  *out_count = count;
  return 0;
}

void cmdb_json_free_string_array(char **items, size_t count) {
  size_t i;
  if (items == NULL) {
    return;
  }
  for (i = 0; i < count; i++) {
    free(items[i]);
  }
  free(items);
}

char *cmdb_json_print(cJSON *item) {
  if (item == NULL) {
    return NULL;
  }
  return cJSON_PrintUnformatted(item);
}

void cmdb_json_wipe_string(cJSON *object, const char *key) {
  cJSON *item;
  if (!cJSON_IsObject(object)) {
    return;
  }
  item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (cJSON_IsString(item) && item->valuestring != NULL) {
    cmdb_crypto_wipe(item->valuestring, strlen(item->valuestring));
  }
}
