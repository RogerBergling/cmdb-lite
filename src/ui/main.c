#include "cmdb/api.h"
#include "cmdb/crypto.h"
#include "cmdb/error.h"
#include "cmdb/util.h"
#include "cmdb/web_bundle.h"
#include "platform_dialog.h"
#include "ui_common.h"

#include "webview/webview.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

typedef struct {
  webview_t webview;
  cmdb_app_t *app;
} shell_t;

static void fatal_message(const char *message) {
  const char *smoke = getenv("CMDB_SMOKE_TEST");
  (void)fprintf(stderr, "cmdb-lite: %s\n", message);
#ifdef _WIN32
  if (smoke == NULL || strcmp(smoke, "1") != 0) {
    (void)MessageBoxA(NULL, message, "cmdb-lite", MB_OK | MB_ICONERROR);
  }
#else
  (void)smoke;
#endif
}

static const char smoke_js[] =
  "(function(){var n=0;function finish(r){"
  "if(window.__cmdb_smoke){window.__cmdb_smoke(JSON.stringify(r));}}"
  "function check(){n++;"
  "if(window.__CMDB_READY__||n>50){"
  "var r={ready:!!window.__CMDB_READY__,title:document.title,"
  "unlockVisible:!document.getElementById('unlock-screen').classList.contains('hidden'),"
  "mainVisible:!document.getElementById('main-app').classList.contains('hidden'),"
  "error:document.getElementById('unlock-error').textContent||'',"
  "toast:document.getElementById('toast').textContent||''};"
  "if(window.__CMDB_SMOKE_DB__){"
  "api('db.create',{path:window.__CMDB_SMOKE_DB__,"
  "passphrase:'correct horse battery staple'})"
  ".then(function(res){r.createdPath=res.path;return api('db.lock');})"
  ".then(function(){r.database='ok';finish(r);})"
  ".catch(function(e){r.database='error:'+(e.message||e);finish(r);});"
  "}else{finish(r);}return;}setTimeout(check,100);}"
  "setTimeout(check,100);})();";

static const char error_page[] =
  "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
  "<title>cmdb-lite</title></head>"
  "<body style=\"font-family:sans-serif;background:#0b141c;color:#e7f1f7;"
  "padding:2rem\"><h1>cmdb-lite</h1>"
  "<p>The application web assets could not be found.</p>"
  "<p>Reinstall the application. Debug builds may instead set "
  "<code>CMDB_WEB_DIR</code> to the directory containing "
  "<code>index.html</code>.</p></body></html>";

static int host_choose_file(void *userdata, int save, const char *title,
                            const char *filter_name, const char *pattern,
                            const char *suggested_name, char **path_out,
                            cmdb_error_t *err) {
  (void)userdata;
  *path_out = platform_choose_file(save, title, filter_name, pattern,
                                   suggested_name, err);
  return (err != NULL && err->code != CMDB_OK) ? -1 : 0;
}

static void write_smoke_result(const char *json) {
  const char *result_path = getenv("CMDB_SMOKE_RESULT");
  cmdb_error_t err;
  if (result_path == NULL || result_path[0] == '\0') {
    return;
  }
  cmdb_error_clear(&err);
  if (cmdb_file_write_atomic(result_path, json, strlen(json), 0600, &err) != 0) {
    (void)fprintf(stderr, "cmdb-lite: cannot write smoke result: %s\n",
                  err.message);
  }
}

static void smoke_callback(const char *id, const char *request_json,
                           void *arg) {
  shell_t *shell = arg;
  cJSON *args = cJSON_Parse(request_json);
  cJSON *first = cJSON_IsArray(args) ? cJSON_GetArrayItem(args, 0) : NULL;

  if (cJSON_IsString(first) && first->valuestring != NULL) {
    write_smoke_result(first->valuestring);
    (void)fprintf(stdout, "SMOKE %s\n", first->valuestring);
    (void)fflush(stdout);
  } else {
    write_smoke_result(
      "{\"ready\":false,\"error\":\"invalid smoke response\"}");
    (void)fprintf(stdout,
                  "SMOKE {\"ready\":false,\"error\":\"invalid smoke response\"}\n");
    (void)fflush(stdout);
  }
  cJSON_Delete(args);
  webview_return(shell->webview, id, 0, "null");
  webview_terminate(shell->webview);
}

static void bridge_callback(const char *id, const char *request_json,
                            void *arg) {
  shell_t *shell = arg;
  cJSON *args;
  cJSON *first;
  char *response;

  args = cJSON_Parse(request_json);
  first = cJSON_IsArray(args) ? cJSON_GetArrayItem(args, 0) : NULL;
  if (!cJSON_IsString(first) || first->valuestring == NULL) {
    webview_return(shell->webview, id, 0,
                   "{\"ok\":false,\"error\":{\"code\":\"invalid_argument\","
                   "\"message\":\"Invalid bridge request\"}}");
    cJSON_Delete(args);
    return;
  }

  response = cmdb_app_handle(shell->app, first->valuestring,
                             strlen(first->valuestring));
  cmdb_crypto_wipe(first->valuestring, strlen(first->valuestring));
  cJSON_Delete(args);

  if (response == NULL) {
    webview_return(shell->webview, id, 0,
                   "{\"ok\":false,\"error\":{\"code\":\"internal\","
                   "\"message\":\"Cannot handle request\"}}");
    return;
  }
  webview_return(shell->webview, id, 0, response);
  free(response);
}

static int run_application(void) {
  webview_t webview;
  shell_t shell;
  cmdb_api_host_t host;
  cmdb_error_t err;
  char *url;
  int debug = 0;
  int server_mode = 0;

#ifndef NDEBUG
  {
    const char *devtools = getenv("CMDB_DEVTOOLS");
    if (devtools != NULL && strcmp(devtools, "1") == 0) {
      debug = 1;
    }
  }
#endif

  webview = webview_create(debug, NULL);
  if (webview == NULL) {
    fatal_message("Cannot create the application webview. "
                  "On Windows, install the WebView2 runtime.");
    return 1;
  }
  (void)webview_set_title(webview, "cmdb-lite");
  (void)webview_set_size(webview, 1280, 820, WEBVIEW_HINT_NONE);
  platform_configure_webview(webview);

  cmdb_error_clear(&err);
  memset(&host, 0, sizeof(host));
  host.choose_file = host_choose_file;
  shell.webview = webview;
  shell.app = cmdb_app_new(&host, &err);

  url = NULL;
  {
    const char *server_url = getenv("CMDB_SERVER_URL");
    if (server_url != NULL && server_url[0] != '\0') {
      url = cmdb_strdup(server_url);
      server_mode = 1;
    }
  }
#ifndef NDEBUG
  if (url == NULL) {
    const char *url_override = getenv("CMDB_UI_URL");
    if (url_override != NULL && url_override[0] != '\0') {
      url = cmdb_strdup(url_override);
    }
  }
  if (url == NULL) {
    const char *web_dir = getenv("CMDB_WEB_DIR");
    if (web_dir != NULL && web_dir[0] != '\0') {
      url = ui_index_url();
    }
  }
#endif
  if (shell.app == NULL) {
    fatal_message(err.message[0] != '\0' ? err.message
                                        : "Cannot initialize the application.");
    (void)webview_set_html(webview, error_page);
    (void)webview_run(webview);
    free(url);
    (void)webview_destroy(webview);
    return 1;
  }

  if (!server_mode &&
      webview_bind(webview, "__cmdb_bridge", bridge_callback, &shell) !=
        WEBVIEW_ERROR_OK) {
    fatal_message("Cannot bind the application bridge.");
    free(url);
    cmdb_app_free(shell.app);
    (void)webview_destroy(webview);
    return 1;
  }

  if (!server_mode) {
    const char *smoke = getenv("CMDB_SMOKE_TEST");
    if (smoke != NULL && strcmp(smoke, "1") == 0) {
      if (webview_bind(webview, "__cmdb_smoke", smoke_callback, &shell) !=
          WEBVIEW_ERROR_OK) {
        fatal_message("Cannot bind the smoke-test bridge.");
        free(url);
        cmdb_app_free(shell.app);
        (void)webview_destroy(webview);
        return 1;
      }
      {
        const char *smoke_db = getenv("CMDB_SMOKE_DB");
        char *init_js;
        if (smoke_db != NULL && smoke_db[0] != '\0') {
          cJSON *quoted = cJSON_CreateString(smoke_db);
          char *quoted_text = quoted != NULL ? cJSON_PrintUnformatted(quoted) : NULL;
          if (quoted_text != NULL) {
            size_t size = strlen(smoke_js) + strlen(quoted_text) + 64;
            init_js = cmdb_malloc(size);
            (void)snprintf(init_js, size,
                           "window.__CMDB_SMOKE_DB__=%s;\n%s",
                           quoted_text, smoke_js);
            (void)webview_init(webview, init_js);
            free(init_js);
            free(quoted_text);
          }
          cJSON_Delete(quoted);
        } else {
          (void)webview_init(webview, smoke_js);
        }
      }
    }
  }

  {
    const char *debug_ui = getenv("CMDB_DEBUG_UI");
    if (debug_ui != NULL && strcmp(debug_ui, "1") == 0) {
      (void)fprintf(stderr, "cmdb-lite: loading %s\n",
                    url != NULL ? url : "embedded web UI");
    }
  }
  if (url != NULL) {
    if (webview_navigate(webview, url) != WEBVIEW_ERROR_OK) {
      fatal_message("Cannot load the application UI.");
      (void)webview_set_html(webview, error_page);
    }
    free(url);
  } else if (webview_set_html(webview, cmdb_web_bundle_html) !=
             WEBVIEW_ERROR_OK) {
    fatal_message("Cannot load the embedded application UI.");
    (void)webview_set_html(webview, error_page);
  }

  (void)webview_run(webview);
  (void)webview_destroy(webview);
  cmdb_app_free(shell.app);
  return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line,
                   int show_command) {
  (void)instance;
  (void)previous;
  (void)command_line;
  (void)show_command;
  return run_application();
}
#else
int main(void) {
  return run_application();
}
#endif
