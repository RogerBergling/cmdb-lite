#include "cmdb/api.h"
#include "cmdb/error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/types.h>
#endif

static void rpc_loop(void) {
  cmdb_app_t *app;
  cmdb_error_t err;
  char *line = NULL;
  size_t capacity = 0;
  ssize_t length;
  int exit_code = 0;

  cmdb_error_clear(&err);
  app = cmdb_app_new(NULL, &err);
  if (app == NULL) {
    (void)fprintf(stderr, "cmdb-rpc: %s\n",
                  err.message[0] != '\0' ? err.message : "cannot start");
    exit(1);
  }

  while ((length = getline(&line, &capacity, stdin)) >= 0) {
    char *response;
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
      line[--length] = '\0';
    }
    if (length == 0) {
      continue;
    }
    response = cmdb_app_handle(app, line, (size_t)length);
    if (response == NULL) {
      (void)fprintf(stderr, "cmdb-rpc: cannot encode response\n");
      exit_code = 1;
      break;
    }
    (void)puts(response);
    (void)fflush(stdout);
    free(response);
  }

  free(line);
  cmdb_app_free(app);
  if (ferror(stdin)) {
    exit_code = 1;
  }
  exit(exit_code ? 1 : 0);
}

#ifdef _WIN32
int main(void) {
  rpc_loop();
  return 0;
}
#else
int main(void) {
  rpc_loop();
  return 0;
}
#endif
