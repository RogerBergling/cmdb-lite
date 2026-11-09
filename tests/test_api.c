#include "test.h"
#include "test_util.h"

#include "cmdb/api.h"
#include "cmdb/types.h"
#include "cmdb/util.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char choose_path[4096];

static int choose_file_stub(void *userdata, int save, const char *title,
                            const char *filter_name, const char *pattern,
                            const char *suggested_name, char **path_out,
                            cmdb_error_t *err) {
  (void)userdata;
  (void)save;
  (void)title;
  (void)filter_name;
  (void)pattern;
  (void)suggested_name;
  (void)err;
  *path_out = cmdb_strdup(choose_path);
  return 0;
}

static void json_escape(const char *input, char *output, size_t output_size) {
  size_t out = 0;
  const unsigned char *p = (const unsigned char *)input;

  while (*p != '\0') {
    const char *escape = NULL;
    char encoded[7];
    switch (*p) {
      case '\\': escape = "\\\\"; break;
      case '"': escape = "\\\""; break;
      case '\b': escape = "\\b"; break;
      case '\f': escape = "\\f"; break;
      case '\n': escape = "\\n"; break;
      case '\r': escape = "\\r"; break;
      case '\t': escape = "\\t"; break;
      default: break;
    }
    if (escape == NULL && *p < 0x20) {
      (void)snprintf(encoded, sizeof(encoded), "\\u%04x",
                     (unsigned int)*p);
      escape = encoded;
    }
    if (escape != NULL) {
      size_t len = strlen(escape);
      if (out + len + 1 >= output_size) {
        break;
      }
      memcpy(output + out, escape, len);
      out += len;
    } else {
      if (out + 2 >= output_size) {
        break;
      }
      output[out++] = (char)*p;
    }
    p++;
  }
  output[out] = '\0';
}

static cJSON *api_call(cmdb_app_t *app, const char *request) {
  char *response_text;
  cJSON *response;

  response_text = cmdb_app_handle(app, request, strlen(request));
  TEST_ASSERT(response_text != NULL);
  if (response_text == NULL) {
    return NULL;
  }
  response = cJSON_Parse(response_text);
  free(response_text);
  TEST_ASSERT(response != NULL);
  return response;
}

void test_api(void) {
  char *dir = test_temp_dir();
  char path[4096];
  char request[16384];
  char json_path[8192];
  cmdb_app_t *app;
  cmdb_error_t err;
  cJSON *response;
  cJSON *result;
  cJSON *ok;
  cmdb_ci_type_t type;
  char type_id[37];
  char ci_id[37] = "";
  char response_id[37] = "";
  cmdb_api_host_t host;
  char drawio_path[4096];

  TEST_ASSERT(dir != NULL);
  if (dir == NULL) {
    return;
  }
  test_path(path, sizeof(path), dir, "api.cmdb");
  test_path(drawio_path, sizeof(drawio_path), dir, "map.drawio");
  memset(&host, 0, sizeof(host));
  host.choose_file = choose_file_stub;
  cmdb_error_clear(&err);
  app = cmdb_app_new(&host, &err);
  TEST_ASSERT(app != NULL);
  if (app == NULL) {
    free(dir);
    return;
  }

  response = api_call(app, "{\"id\":1,\"method\":\"app.info\",\"params\":{}}");
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsTrue(ok));
    cJSON_Delete(response);
  }

  json_escape(path, json_path, sizeof(json_path));
  (void)snprintf(request, sizeof(request),
                 "{\"id\":2,\"method\":\"db.create\",\"params\":{"
                 "\"path\":\"%s\",\"passphrase\":\"%s\"}}",
                 json_path, TEST_PASSPHRASE);
  response = api_call(app, request);
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsTrue(ok));
    cJSON_Delete(response);
  }
  TEST_ASSERT(cmdb_app_db(app) != NULL);

  response = api_call(app, "{\"id\":3,\"method\":\"types.list\",\"params\":{}}");
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    result = cJSON_GetObjectItemCaseSensitive(response, "result");
    TEST_ASSERT(cJSON_IsObject(result));
    TEST_ASSERT(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(result, "items")) >= 12);
    cJSON_Delete(response);
  }

  TEST_ASSERT(cmdb_types_find_by_name(cmdb_app_db(app), "Linux Server",
                                      &type, &err) == 0);
  (void)cmdb_str_copy(type_id, sizeof(type_id), type.id);
  (void)snprintf(request, sizeof(request),
                 "{\"id\":4,\"method\":\"cis.create\",\"params\":{"
                 "\"name\":\"api-app\",\"type_id\":\"%s\","
                 "\"description\":\"Created via API\","
                 "\"attributes\":{\"ip_address\":\"10.0.0.99\"}}}",
                 type_id);
  response = api_call(app, request);
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsTrue(ok));
    result = cJSON_GetObjectItemCaseSensitive(response, "result");
    if (cJSON_IsObject(result)) {
      cJSON *id = cJSON_GetObjectItemCaseSensitive(result, "id");
      TEST_ASSERT(cJSON_IsString(id));
      if (cJSON_IsString(id)) {
        (void)cmdb_str_copy(response_id, sizeof(response_id), id->valuestring);
      }
    }
    cJSON_Delete(response);
  }
  (void)cmdb_str_copy(ci_id, sizeof(ci_id), response_id);

  response = api_call(app, "{\"id\":5,\"method\":\"cis.list\",\"params\":{}}");
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    result = cJSON_GetObjectItemCaseSensitive(response, "result");
    TEST_ASSERT(cJSON_IsObject(result));
    if (cJSON_IsObject(result)) {
      cJSON *total = cJSON_GetObjectItemCaseSensitive(result, "total");
      TEST_ASSERT(cJSON_IsNumber(total));
      TEST_ASSERT(total != NULL && total->valueint == 1);
    }
    cJSON_Delete(response);
  }

  (void)snprintf(request, sizeof(request),
                 "{\"id\":6,\"method\":\"graph.dependencies\",\"params\":{"
                 "\"ids\":[\"%s\"],\"depth\":2,\"direction\":\"both\"}}",
                 ci_id);
  response = api_call(app, request);
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsTrue(ok));
    result = cJSON_GetObjectItemCaseSensitive(response, "result");
    TEST_ASSERT(cJSON_IsObject(result));
    if (cJSON_IsObject(result)) {
      cJSON *nodes = cJSON_GetObjectItemCaseSensitive(result, "nodes");
      TEST_ASSERT(cJSON_GetArraySize(nodes) == 1);
    }
    cJSON_Delete(response);
  }

  response = api_call(app, "{\"id\":20,\"method\":\"scan.nmap\",\"params\":{"
                            "\"target\":\"10.20.0.0/24\"}}");
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsFalse(ok));
    cJSON_Delete(response);
  }

  (void)snprintf(request, sizeof(request),
                 "{\"id\":21,\"method\":\"scan.import\",\"params\":{"
                 "\"target\":\"10.20.0.0/24\",\"type_id\":\"%s\","
                 "\"name_prefix\":\"scan-\",\"hosts\":[{"
                 "\"ip_address\":\"10.20.0.77\","
                 "\"hostname\":\"imported-app\","
                 "\"mac_address\":\"aa:bb:cc:dd:ee:77\","
                 "\"os_guess\":\"Linux 6.8\","
                 "\"open_ports\":[\"22/tcp\",\"443/tcp\"]}]}}",
                 type_id);
  response = api_call(app, request);
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsTrue(ok));
    result = cJSON_GetObjectItemCaseSensitive(response, "result");
    TEST_ASSERT(cJSON_IsObject(result));
    if (cJSON_IsObject(result)) {
      cJSON *created = cJSON_GetObjectItemCaseSensitive(result, "created");
      TEST_ASSERT(cJSON_IsNumber(created));
      TEST_ASSERT(created != NULL && created->valueint == 1);
    }
    cJSON_Delete(response);
  }

  response = api_call(app, "{\"id\":22,\"method\":\"cis.list\",\"params\":{"
                            "\"query\":\"scan-imported-app\"}}");
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    result = cJSON_GetObjectItemCaseSensitive(response, "result");
    if (cJSON_IsObject(result)) {
      cJSON *total = cJSON_GetObjectItemCaseSensitive(result, "total");
      TEST_ASSERT(cJSON_IsNumber(total));
      TEST_ASSERT(total != NULL && total->valueint == 1);
    }
    cJSON_Delete(response);
  }

  response = api_call(app, "{\"id\":7,\"method\":\"security.info\",\"params\":{}}");
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsTrue(ok));
    cJSON_Delete(response);
  }

  (void)cmdb_str_copy(choose_path, sizeof(choose_path), path);
  (void)snprintf(request, sizeof(request),
                 "{\"id\":11,\"method\":\"graph.save_drawio\",\"params\":{"
                 "\"ids\":[\"%s\"],\"depth\":1,\"direction\":\"both\"}}",
                 ci_id);
  response = api_call(app, request);
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsFalse(ok));
    cJSON_Delete(response);
  }

  (void)cmdb_str_copy(choose_path, sizeof(choose_path), drawio_path);
  response = api_call(app, request);
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsTrue(ok));
    cJSON_Delete(response);
  }
  TEST_ASSERT(cmdb_is_regular_file(drawio_path));
  TEST_ASSERT(test_file_contains(drawio_path, "<mxfile"));

  response = api_call(app, "{\"id\":10,\"method\":\"audit.verify\",\"params\":{}}");
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsTrue(ok));
    result = cJSON_GetObjectItemCaseSensitive(response, "result");
    TEST_ASSERT(cJSON_IsObject(result));
    if (cJSON_IsObject(result)) {
      cJSON *valid = cJSON_GetObjectItemCaseSensitive(result, "valid");
      TEST_ASSERT(cJSON_IsTrue(valid));
    }
    cJSON_Delete(response);
  }

  response = api_call(app, "not json");
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsFalse(ok));
    cJSON_Delete(response);
  }

  response = api_call(app, "{\"id\":8,\"method\":\"db.lock\",\"params\":{}}");
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsTrue(ok));
    cJSON_Delete(response);
  }
  TEST_ASSERT(cmdb_app_db(app) == NULL);

  response = api_call(app, "{\"id\":9,\"method\":\"cis.list\",\"params\":{}}");
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsFalse(ok));
    cJSON_Delete(response);
  }

  cmdb_app_free(app);
  (void)remove(drawio_path);
  test_cleanup_db(dir, "api.cmdb");
  free(dir);
}
