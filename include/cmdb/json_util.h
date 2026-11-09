#ifndef CMDB_JSON_UTIL_H
#define CMDB_JSON_UTIL_H

#include "cmdb/error.h"

#include "cJSON.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

cJSON *cmdb_json_get_params(cJSON *request);
int cmdb_json_require_object(cJSON *item, const char *what,
                             cmdb_error_t *err);
int cmdb_json_string(cJSON *object, const char *key, const char **out,
                     int required, cmdb_error_t *err);
int cmdb_json_string_len(cJSON *object, const char *key, const char **out,
                         size_t *out_len, int required, cmdb_error_t *err);
int cmdb_json_int(cJSON *object, const char *key, int default_value,
                  int min_value, int max_value, int *out, cmdb_error_t *err);
int cmdb_json_bool(cJSON *object, const char *key, int default_value,
                   int *out, cmdb_error_t *err);
int cmdb_json_string_array(cJSON *object, const char *key,
                           char ***out, size_t *out_count,
                           cmdb_error_t *err);
void cmdb_json_free_string_array(char **items, size_t count);
char *cmdb_json_print(cJSON *item);
void cmdb_json_wipe_string(cJSON *object, const char *key);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_JSON_UTIL_H */
