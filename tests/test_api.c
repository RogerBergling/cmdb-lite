#include "test.h"
#include "test_util.h"

#include "cmdb/api.h"
#include "cmdb/ci.h"
#include "cmdb/diagram.h"
#include "cmdb/scan.h"
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
  char backup_path[4096];

  TEST_ASSERT(dir != NULL);
  if (dir == NULL) {
    return;
  }
  test_path(path, sizeof(path), dir, "api.cmdb");
  test_path(drawio_path, sizeof(drawio_path), dir, "map.drawio");
  test_path(backup_path, sizeof(backup_path), dir, "backup.cmdb");
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
                 "\"business_service\":\"Existing Service\","
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

  response = api_call(app, "{\"id\":20,\"method\":\"scan.start\",\"params\":{"
                            "\"target\":\"10.20.0.0/24\"}}");
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
    TEST_ASSERT(cJSON_IsFalse(ok));
    cJSON_Delete(response);
  }

  {
    cmdb_scan_result_t scan;
    char job_id[37];
    char asset_id[37];
    char matched_asset_id[37];
    memset(&scan, 0, sizeof(scan));
    memset(&err, 0, sizeof(err));
    err.code = CMDB_OK;
    TEST_ASSERT(cmdb_scan_store_begin(cmdb_app_db(app), "10.20.0.0/24",
                                      "22,443", 1, 0, job_id, &err) == 0);
    (void)cmdb_str_copy(scan.job_id, sizeof(scan.job_id), job_id);
    (void)cmdb_str_copy(scan.target, sizeof(scan.target), "10.20.0.0/24");
    (void)cmdb_str_copy(scan.ports, sizeof(scan.ports), "22,443");
    (void)cmdb_str_copy(scan.method, sizeof(scan.method), "nmap");
    scan.service_detection = 1;
    (void)cmdb_iso8601_now(scan.started_at);
    scan.hosts = cmdb_calloc(2, sizeof(*scan.hosts));
    scan.host_count = 2;
    (void)cmdb_str_copy(scan.hosts[0].ip_address,
                        sizeof(scan.hosts[0].ip_address), "10.20.0.77");
    (void)cmdb_str_copy(scan.hosts[0].hostname,
                        sizeof(scan.hosts[0].hostname), "imported-app");
    (void)cmdb_str_copy(scan.hosts[0].mac_address,
                        sizeof(scan.hosts[0].mac_address),
                        "aa:bb:cc:dd:ee:77");
    (void)cmdb_str_copy(scan.hosts[0].os_guess,
                        sizeof(scan.hosts[0].os_guess), "Linux 6.8");
    (void)cmdb_str_copy(scan.hosts[0].open_ports,
                        sizeof(scan.hosts[0].open_ports), "22/tcp,443/tcp");
    scan.hosts[0].port_count = 2;
    (void)cmdb_str_copy(scan.hosts[1].ip_address,
                        sizeof(scan.hosts[1].ip_address), "10.0.0.99");
    (void)cmdb_str_copy(scan.hosts[1].hostname,
                        sizeof(scan.hosts[1].hostname), "api-app");
    (void)cmdb_str_copy(scan.hosts[1].open_ports,
                        sizeof(scan.hosts[1].open_ports), "443/tcp");
    scan.hosts[1].port_count = 1;
    TEST_ASSERT(cmdb_scan_store_finish(cmdb_app_db(app), &scan,
                                       "completed", NULL, &err) == 0);
    TEST_ASSERT(scan.hosts[1].match_count > 0);
    (void)cmdb_str_copy(asset_id, sizeof(asset_id),
                        scan.hosts[0].asset_id);
    (void)cmdb_str_copy(matched_asset_id, sizeof(matched_asset_id),
                        scan.hosts[1].asset_id);
    cmdb_scan_result_free(&scan);

    response = api_call(app, "{\"id\":21,\"method\":\"scan.jobs\",\"params\":{}}");
    TEST_ASSERT(response != NULL);
    if (response != NULL) {
      result = cJSON_GetObjectItemCaseSensitive(response, "result");
      TEST_ASSERT(cJSON_IsObject(result));
      if (cJSON_IsObject(result)) {
        cJSON *items = cJSON_GetObjectItemCaseSensitive(result, "items");
        TEST_ASSERT(cJSON_GetArraySize(items) >= 1);
      }
      cJSON_Delete(response);
    }

    (void)snprintf(request, sizeof(request),
                   "{\"id\":22,\"method\":\"scan.import\",\"params\":{"
                   "\"job_id\":\"%s\",\"type_id\":\"%s\","
                   "\"decisions\":[{\"asset_id\":\"%s\","
                   "\"action\":\"create\"},{\"asset_id\":\"%s\","
                   "\"action\":\"merge\",\"ci_id\":\"%s\"}]}}",
                   job_id, type_id, asset_id, matched_asset_id, ci_id);
    response = api_call(app, request);
    TEST_ASSERT(response != NULL);
    if (response != NULL) {
      ok = cJSON_GetObjectItemCaseSensitive(response, "ok");
      TEST_ASSERT(cJSON_IsTrue(ok));
      result = cJSON_GetObjectItemCaseSensitive(response, "result");
      TEST_ASSERT(cJSON_IsObject(result));
      if (cJSON_IsObject(result)) {
        cJSON *created = cJSON_GetObjectItemCaseSensitive(result, "created");
        cJSON *merged = cJSON_GetObjectItemCaseSensitive(result, "merged");
        TEST_ASSERT(cJSON_IsNumber(created));
        TEST_ASSERT(created != NULL && created->valueint == 1);
        TEST_ASSERT(cJSON_IsNumber(merged));
        TEST_ASSERT(merged != NULL && merged->valueint == 1);
      }
      cJSON_Delete(response);
    }
  }

  (void)snprintf(request, sizeof(request),
                 "{\"id\":24,\"method\":\"cis.get\",\"params\":{\"id\":\"%s\"}}",
                 ci_id);
  response = api_call(app, request);
  TEST_ASSERT(response != NULL);
  if (response != NULL) {
    result = cJSON_GetObjectItemCaseSensitive(response, "result");
    if (cJSON_IsObject(result)) {
      cJSON *service = cJSON_GetObjectItemCaseSensitive(result,
                                                        "business_service");
      TEST_ASSERT(cJSON_IsString(service));
      TEST_ASSERT(service != NULL &&
                  strcmp(service->valuestring, "Existing Service") == 0);
    }
    cJSON_Delete(response);
  }

  response = api_call(app, "{\"id\":23,\"method\":\"cis.list\",\"params\":{"
                            "\"query\":\"imported-app\"}}");
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

  {
    char diagram_id[37];
    cmdb_diagram_t diagram;
    cmdb_diagram_t *list = NULL;
    size_t list_count = 0;
    cmdb_error_clear(&err);
    TEST_ASSERT(cmdb_diagram_save(cmdb_app_db(app), NULL, "Test diagram",
                                  "<mxfile><diagram><mxGraphModel><root>"
                                  "<mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
                                  "</root></mxGraphModel></diagram></mxfile>",
                                  diagram_id, &err) == 0);
    TEST_ASSERT(cmdb_diagram_get(cmdb_app_db(app), diagram_id, &diagram,
                                 &err) == 0);
    TEST_ASSERT(strcmp(diagram.name, "Test diagram") == 0);
    cmdb_diagram_free(&diagram);
    TEST_ASSERT(cmdb_diagram_list(cmdb_app_db(app), &list, &list_count, 0,
                                  &err) == 0);
    TEST_ASSERT(list_count == 1);
    cmdb_diagram_free_list(list, list_count);
    TEST_ASSERT(cmdb_diagram_delete(cmdb_app_db(app), diagram_id, &err) == 0);
  }

  {
    cmdb_db_t *backup_db = NULL;
    size_t count = 0;
    cmdb_error_clear(&err);
    TEST_ASSERT(cmdb_db_backup(cmdb_app_db(app), backup_path, &err) == 0);
    TEST_ASSERT(cmdb_db_open(backup_path, TEST_PASSPHRASE, &backup_db,
                             &err) == 0);
    if (backup_db != NULL) {
      TEST_ASSERT(cmdb_ci_count(backup_db, &count, &err) == 0);
      TEST_ASSERT(count == 2);
      cmdb_db_close(backup_db);
    }
  }
  test_cleanup_db(dir, "backup.cmdb");

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
