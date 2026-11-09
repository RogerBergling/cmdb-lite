#include "ui_common.h"

#include "cmdb/util.h"

#include <stdlib.h>
#include <string.h>

#ifndef CMDB_WEB_DIR
#define CMDB_WEB_DIR ""
#endif

#ifndef CMDB_INSTALL_WEB_DIR
#define CMDB_INSTALL_WEB_DIR ""
#endif

static int has_index(const char *root) {
  char *index;
  int exists;
  if (root == NULL || root[0] == '\0') {
    return 0;
  }
  index = cmdb_path_join(root, "index.html");
  exists = cmdb_is_regular_file(index);
  free(index);
  return exists;
}

static char *try_root(const char *root) {
  if (has_index(root)) {
    return cmdb_strdup(root);
  }
  return NULL;
}

char *ui_resolve_web_root(void) {
  const char *env = getenv("CMDB_WEB_DIR");
  char *exe_dir;
  char *candidate;
  char *result;

  result = try_root(env);
  if (result != NULL) {
    return result;
  }
  result = try_root(CMDB_WEB_DIR);
  if (result != NULL) {
    return result;
  }

  exe_dir = cmdb_executable_dir();
  if (exe_dir == NULL) {
    return NULL;
  }

  candidate = cmdb_path_join(exe_dir, "web");
  result = try_root(candidate);
  free(candidate);
  if (result != NULL) {
    free(exe_dir);
    return result;
  }

  candidate = cmdb_path_join(exe_dir, "../share/cmdb-lite/web");
  result = try_root(candidate);
  free(candidate);
  free(exe_dir);
  if (result != NULL) {
    return result;
  }

  result = try_root(CMDB_INSTALL_WEB_DIR);
  if (result != NULL) {
    return result;
  }

  result = try_root("/usr/local/share/cmdb-lite/web");
  if (result != NULL) {
    return result;
  }

  return try_root("/usr/share/cmdb-lite/web");
}

char *ui_index_url(void) {
  char *root = ui_resolve_web_root();
  char *index;
  char *url;
  if (root == NULL) {
    return NULL;
  }
  index = cmdb_path_join(root, "index.html");
  url = cmdb_path_to_file_url(index);
  free(index);
  free(root);
  return url;
}
