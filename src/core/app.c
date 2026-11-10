#include "cmdb/api.h"

#include "app_internal.h"
#include "cmdb/crypto.h"
#include "cmdb/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CMDB_VERSION
#define CMDB_VERSION "0.0.0"
#endif

#define CMDB_MAX_API_BYTES (64u * 1024u * 1024u)

cmdb_app_t *cmdb_app_new(const cmdb_api_host_t *host, cmdb_error_t *err) {
  cmdb_app_t *app;

  if (cmdb_crypto_init(err) != 0) {
    return NULL;
  }
  app = cmdb_calloc(1, sizeof(*app));
  if (host != NULL) {
    app->host = *host;
  }
  app->scan_manager = cmdb_scan_manager_new();
  if (app->scan_manager == NULL) {
    free(app);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create scan manager");
    return NULL;
  }
  return app;
}

void cmdb_app_free(cmdb_app_t *app) {
  if (app == NULL) {
    return;
  }
  if (app->scan_manager != NULL) {
    cmdb_scan_manager_free(app->scan_manager);
    app->scan_manager = NULL;
  }
  if (app->db != NULL) {
    cmdb_db_close(app->db);
    app->db = NULL;
  }
  free(app);
}

cmdb_db_t *cmdb_app_db(cmdb_app_t *app) {
  return app != NULL ? app->db : NULL;
}

static cJSON *error_response(cJSON *id, cmdb_error_t *err) {
  cJSON *response = cJSON_CreateObject();
  cJSON *error = cJSON_CreateObject();

  if (response == NULL || error == NULL) {
    cJSON_Delete(response);
    cJSON_Delete(error);
    return NULL;
  }
  cJSON_AddBoolToObject(response, "ok", 0);
  if (id != NULL) {
    cJSON *copy = cJSON_Duplicate(id, 1);
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

char *cmdb_app_handle(cmdb_app_t *app, const char *request,
                      size_t request_len) {
  cJSON *parsed = NULL;
  cJSON *response = NULL;
  char *printed = NULL;
  cmdb_error_t err;

  cmdb_error_clear(&err);
  if (app == NULL || request == NULL) {
    cmdb_error_set(&err, CMDB_ERR_INVALID_ARGUMENT, "Invalid API request");
    response = error_response(NULL, &err);
    goto print;
  }
  if (request_len == 0 || request_len > CMDB_MAX_API_BYTES) {
    cmdb_error_set(&err, CMDB_ERR_LIMIT, "API request is too large");
    response = error_response(NULL, &err);
    goto print;
  }

  parsed = cJSON_ParseWithLengthOpts(request, request_len, NULL, 0);
  if (parsed == NULL) {
    cmdb_error_set(&err, CMDB_ERR_INVALID_ARGUMENT, "Invalid JSON request");
    response = error_response(NULL, &err);
    goto print;
  }
  if (!cJSON_IsObject(parsed)) {
    cmdb_error_set(&err, CMDB_ERR_INVALID_ARGUMENT,
                   "API request must be a JSON object");
    response = error_response(NULL, &err);
    goto print;
  }

  response = cmdb_api_handle(app, parsed);
  if (response == NULL) {
    cmdb_error_set(&err, CMDB_ERR_INTERNAL, "API handler failed");
    response = error_response(NULL, &err);
  }

print:
  cJSON_Delete(parsed);
  if (response != NULL) {
    printed = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
  }
  if (printed == NULL) {
    printed = cmdb_strdup(
      "{\"ok\":false,\"error\":{\"code\":\"internal\","
      "\"message\":\"Cannot encode API response\"}}");
  }
  return printed;
}
