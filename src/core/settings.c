#include "cmdb/settings.h"

#include "cmdb/util.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define CMDB_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define CMDB_MKDIR(path) mkdir(path, 0700)
#endif

#define CMDB_SETTINGS_VERSION 1

static int settings_make_dir(const char *path) {
  if (path == NULL || path[0] == '\0') {
    return -1;
  }
  if (cmdb_path_exists(path)) {
    return 0;
  }
  if (CMDB_MKDIR(path) == 0) {
    return 0;
  }
  return cmdb_path_exists(path) ? 0 : -1;
}

char *cmdb_settings_config_path(void) {
  const char *base = getenv("CMDB_CONFIG_DIR");
  const char *home;
  char path[CMDB_MAX_PATH];
  char directory[CMDB_MAX_PATH];
  int written;

#ifdef _WIN32
  if (base == NULL || base[0] == '\0') {
    base = getenv("APPDATA");
  }
  if (base == NULL || base[0] == '\0') {
    home = getenv("USERPROFILE");
    if (home != NULL && home[0] != '\0') {
      written = snprintf(directory, sizeof(directory), "%s/cmdb-lite", home);
      if (written < 0 || (size_t)written >= sizeof(directory)) {
        return NULL;
      }
      (void)settings_make_dir(directory);
      written = snprintf(path, sizeof(path), "%s/config.json", directory);
      if (written < 0 || (size_t)written >= sizeof(path)) {
        return NULL;
      }
      return cmdb_strdup(path);
    }
  }
#else
  if (base == NULL || base[0] == '\0') {
    base = getenv("XDG_CONFIG_HOME");
  }
  if (base == NULL || base[0] == '\0') {
    home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
      written = snprintf(directory, sizeof(directory), "%s/.config", home);
      if (written < 0 || (size_t)written >= sizeof(directory)) {
        return NULL;
      }
      base = directory;
    }
  }
#endif
  if (base == NULL || base[0] == '\0') {
    return NULL;
  }
  written = snprintf(directory, sizeof(directory), "%s/cmdb-lite", base);
  if (written < 0 || (size_t)written >= sizeof(directory)) {
    return NULL;
  }
  (void)settings_make_dir(directory);
  written = snprintf(path, sizeof(path), "%s/config.json", directory);
  if (written < 0 || (size_t)written >= sizeof(path)) {
    return NULL;
  }
  return cmdb_strdup(path);
}

int cmdb_settings_load(cmdb_settings_t *settings, cmdb_error_t *err) {
  char *path;
  char *data = NULL;
  size_t len = 0;
  cJSON *root;
  cJSON *last;
  cJSON *recent;
  cJSON *item;
  int rc;

  if (settings == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid settings pointer");
    return -1;
  }
  memset(settings, 0, sizeof(*settings));
  path = cmdb_settings_config_path();
  if (path == NULL) {
    return 0;
  }
  if (!cmdb_path_exists(path)) {
    free(path);
    return 0;
  }
  if (cmdb_file_read_all(path, 64u * 1024u, &data, &len, err) != 0) {
    free(path);
    return -1;
  }
  root = cJSON_ParseWithLength(data, len);
  free(data);
  free(path);
  if (root == NULL || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return 0;
  }
  last = cJSON_GetObjectItemCaseSensitive(root, "last_database");
  if (cJSON_IsString(last) && last->valuestring != NULL) {
    (void)cmdb_str_copy(settings->last_database,
                        sizeof(settings->last_database), last->valuestring);
  }
  recent = cJSON_GetObjectItemCaseSensitive(root, "recent_databases");
  cJSON_ArrayForEach(item, recent) {
    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        item->valuestring[0] == '\0') {
      continue;
    }
    if (settings->recent_count >= CMDB_MAX_RECENT_DATABASES) {
      break;
    }
    (void)cmdb_str_copy(settings->recent_databases[settings->recent_count],
                        sizeof(settings->recent_databases[0]),
                        item->valuestring);
    settings->recent_count++;
  }
  cJSON_Delete(root);
  rc = 0;
  return rc;
}

int cmdb_settings_save(const cmdb_settings_t *settings, cmdb_error_t *err) {
  char *path;
  cJSON *root;
  cJSON *recent;
  char *json;
  size_t i;
  int rc;

  if (settings == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid settings pointer");
    return -1;
  }
  path = cmdb_settings_config_path();
  if (path == NULL) {
    cmdb_error_set(err, CMDB_ERR_IO,
                   "Cannot determine the configuration directory");
    return -1;
  }
  root = cJSON_CreateObject();
  if (root == NULL) {
    free(path);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create settings");
    return -1;
  }
  cJSON_AddNumberToObject(root, "version", CMDB_SETTINGS_VERSION);
  cJSON_AddStringToObject(root, "last_database", settings->last_database);
  recent = cJSON_CreateArray();
  if (recent == NULL) {
    cJSON_Delete(root);
    free(path);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create settings");
    return -1;
  }
  for (i = 0; i < settings->recent_count; i++) {
    cJSON_AddItemToArray(recent,
                         cJSON_CreateString(settings->recent_databases[i]));
  }
  cJSON_AddItemToObject(root, "recent_databases", recent);
  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    free(path);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot encode settings");
    return -1;
  }
  rc = cmdb_file_write_atomic(path, json, strlen(json), 0600, err);
  free(json);
  free(path);
  return rc;
}

void cmdb_settings_remember(cmdb_settings_t *settings, const char *path) {
  size_t i;
  size_t found = (size_t)-1;

  if (settings == NULL || path == NULL || path[0] == '\0') {
    return;
  }
  (void)cmdb_str_copy(settings->last_database,
                      sizeof(settings->last_database), path);
  for (i = 0; i < settings->recent_count; i++) {
    if (strcmp(settings->recent_databases[i], path) == 0) {
      found = i;
      break;
    }
  }
  if (found != (size_t)-1) {
    for (i = found; i > 0; i--) {
      (void)cmdb_str_copy(settings->recent_databases[i],
                          sizeof(settings->recent_databases[i]),
                          settings->recent_databases[i - 1]);
    }
  } else {
    if (settings->recent_count < CMDB_MAX_RECENT_DATABASES) {
      settings->recent_count++;
    }
    for (i = settings->recent_count - 1; i > 0; i--) {
      (void)cmdb_str_copy(settings->recent_databases[i],
                          sizeof(settings->recent_databases[i]),
                          settings->recent_databases[i - 1]);
    }
  }
  (void)cmdb_str_copy(settings->recent_databases[0],
                      sizeof(settings->recent_databases[0]), path);
}
