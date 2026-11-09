#ifndef CMDB_TEST_UTIL_H
#define CMDB_TEST_UTIL_H

#include "cmdb/db.h"
#include "cmdb/error.h"
#include "cmdb/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#define TEST_PASSPHRASE "correct horse battery staple"

static inline char *test_temp_dir(void) {
#ifdef _WIN32
  char temp_path[MAX_PATH];
  char file_path[MAX_PATH];
  DWORD len = GetTempPathA(MAX_PATH, temp_path);
  if (len == 0 || len >= MAX_PATH) {
    return NULL;
  }
  if (GetTempFileNameA(temp_path, "cmdb", 0, file_path) == 0) {
    return NULL;
  }
  DeleteFileA(file_path);
  if (_mkdir(file_path) != 0) {
    return NULL;
  }
  return _strdup(file_path);
#else
  char template_path[] = "/tmp/cmdb-test-XXXXXX";
  char *result = mkdtemp(template_path);
  return result != NULL ? strdup(result) : NULL;
#endif
}

static inline void test_path(char *out, size_t out_size,
                             const char *dir, const char *name) {
  (void)snprintf(out, out_size, "%s/%s", dir, name);
}

static inline cmdb_db_t *test_create_db(const char *dir, const char *name,
                                        char *path, size_t path_size) {
  cmdb_error_t err;
  cmdb_db_t *db = NULL;

  test_path(path, path_size, dir, name);
  cmdb_error_clear(&err);
  if (cmdb_db_create(path, TEST_PASSPHRASE, &db, &err) != 0) {
    (void)fprintf(stderr, "create db failed: %s %s\n",
                  err.message, err.detail);
    return NULL;
  }
  return db;
}

static inline cmdb_db_t *test_open_db(const char *path) {
  cmdb_error_t err;
  cmdb_db_t *db = NULL;
  cmdb_error_clear(&err);
  if (cmdb_db_open(path, TEST_PASSPHRASE, &db, &err) != 0) {
    (void)fprintf(stderr, "open db failed: %s %s\n",
                  err.message, err.detail);
    return NULL;
  }
  return db;
}

static inline void test_cleanup_db(const char *dir, const char *name) {
  char path[4096];
  char meta[4110];
  char wal[4110];
  char shm[4110];

  test_path(path, sizeof(path), dir, name);
  (void)snprintf(meta, sizeof(meta), "%s.meta", path);
  (void)snprintf(wal, sizeof(wal), "%s-wal", path);
  (void)snprintf(shm, sizeof(shm), "%s-shm", path);
  (void)remove(path);
  (void)remove(meta);
  (void)remove(wal);
  (void)remove(shm);
}

static inline int test_file_contains(const char *path, const char *needle) {
  FILE *file = fopen(path, "rb");
  char *data;
  long size;
  int found = 0;

  if (file == NULL) {
    return 0;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return 0;
  }
  size = ftell(file);
  if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return 0;
  }
  data = (char *)malloc((size_t)size + 1);
  if (data == NULL) {
    fclose(file);
    return 0;
  }
  if (fread(data, 1, (size_t)size, file) == (size_t)size) {
    data[size] = '\0';
    found = strstr(data, needle) != NULL;
  }
  free(data);
  fclose(file);
  return found;
}

#endif /* CMDB_TEST_UTIL_H */
