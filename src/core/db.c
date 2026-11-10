#include "cmdb/db.h"

#include "cmdb/crypto.h"
#include "cmdb/limits.h"
#include "cmdb/schema.h"
#include "cmdb/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

struct cmdb_db {
  sqlite3 *handle;
  char *path;
  cmdb_meta_t meta;
  char key_hex[65];
};

static void db_wipe(cmdb_db_t *db) {
  if (db == NULL) {
    return;
  }
  if (db->handle != NULL) {
    sqlite3_close_v2(db->handle);
    db->handle = NULL;
  }
  free(db->path);
  db->path = NULL;
  cmdb_crypto_wipe(db->meta.salt, sizeof(db->meta.salt));
  cmdb_crypto_wipe(db->key_hex, sizeof(db->key_hex));
  db->meta.opslimit = 0;
  db->meta.memlimit = 0;
  db->meta.version = 0;
}

static void db_chmod_aux_files(const char *path) {
#ifndef _WIN32
  char *aux;
  size_t len;
  if (path == NULL) {
    return;
  }
  len = strlen(path) + 8;
  aux = cmdb_malloc(len);
  (void)snprintf(aux, len, "%s-wal", path);
  (void)cmdb_file_chmod_0600(aux);
  (void)snprintf(aux, len, "%s-shm", path);
  (void)cmdb_file_chmod_0600(aux);
  free(aux);
#else
  (void)path;
#endif
}

static void db_remove_aux_files(const char *path) {
  char *aux;
  size_t len;

  if (path == NULL) {
    return;
  }
  len = strlen(path) + 8;
  aux = cmdb_malloc(len);
  (void)snprintf(aux, len, "%s-wal", path);
  (void)cmdb_file_remove(aux);
  (void)snprintf(aux, len, "%s-shm", path);
  (void)cmdb_file_remove(aux);
  free(aux);
}

static int db_apply_pragmas(cmdb_db_t *db, cmdb_error_t *err) {
  static const char *pragmas[] = {
    "PRAGMA cipher_memory_security = ON;",
    "PRAGMA foreign_keys = ON;",
    "PRAGMA secure_delete = ON;",
    "PRAGMA temp_store = MEMORY;",
    "PRAGMA busy_timeout = 5000;",
    "PRAGMA journal_mode = WAL;",
    "PRAGMA synchronous = FULL;",
    "PRAGMA trusted_schema = OFF;",
    "PRAGMA cell_size_check = ON;",
    NULL
  };
  size_t i;

  for (i = 0; pragmas[i] != NULL; i++) {
    if (cmdb_db_exec(db, pragmas[i], err) != 0) {
      return -1;
    }
  }
  return 0;
}

static int db_verify_key(cmdb_db_t *db, cmdb_error_t *err) {
  sqlite3_stmt *stmt = NULL;
  int rc;

  rc = sqlite3_prepare_v2(db->handle,
                          "SELECT count(*) FROM sqlite_master;",
                          -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_AUTH,
                   "Wrong passphrase or not a cmdb-lite database");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(db->handle));
    return -1;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    cmdb_error_set(err, CMDB_ERR_AUTH,
                   "Wrong passphrase or not a cmdb-lite database");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(db->handle));
    return -1;
  }
  sqlite3_finalize(stmt);
  return 0;
}

static int db_set_key(cmdb_db_t *db, const char *passphrase,
                      cmdb_error_t *err) {
  unsigned char key[CMDB_KDF_KEY_LEN];
  char key_hex[65];
  int rc;

  if (cmdb_crypto_derive_key(passphrase, db->meta.salt,
                             db->meta.opslimit, db->meta.memlimit,
                             key, err) != 0) {
    return -1;
  }
  if (cmdb_key_to_hex(key, key_hex, err) != 0) {
    cmdb_crypto_wipe(key, sizeof(key));
    return -1;
  }
  cmdb_crypto_wipe(key, sizeof(key));

  rc = sqlite3_key(db->handle, key_hex, (int)strlen(key_hex));
  if (rc == SQLITE_OK) {
    (void)cmdb_str_copy(db->key_hex, sizeof(db->key_hex), key_hex);
  }
  cmdb_crypto_wipe(key_hex, sizeof(key_hex));
  if (rc != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_AUTH, "Cannot apply database key");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(db->handle));
    return -1;
  }
  return 0;
}

static int db_open_handle(cmdb_db_t *db, int create, cmdb_error_t *err) {
  int flags = SQLITE_OPEN_READWRITE;
  int rc;

  if (create) {
    flags |= SQLITE_OPEN_CREATE;
  }

  rc = sqlite3_open_v2(db->path, &db->handle, flags, NULL);
  if (rc != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_IO, "Cannot open database");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(db->handle));
    if (db->handle != NULL) {
      sqlite3_close_v2(db->handle);
      db->handle = NULL;
    }
    return -1;
  }

  sqlite3_extended_result_codes(db->handle, 1);
  sqlite3_busy_timeout(db->handle, 5000);
  return 0;
}

int cmdb_db_create(const char *path, const char *passphrase,
                   cmdb_db_t **out, cmdb_error_t *err) {
  cmdb_db_t *db;
  int db_created = 0;
  int meta_created = 0;
#ifndef _WIN32
  mode_t old_umask = 0;
  int umask_set = 0;
#endif

  if (out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid output argument");
    return -1;
  }
  *out = NULL;
  if (path == NULL || path[0] == '\0' || strlen(path) >= CMDB_MAX_PATH) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid database path");
    return -1;
  }
  if (passphrase == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Passphrase is required");
    return -1;
  }

  if (cmdb_path_exists(path)) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT, "Database file already exists");
    cmdb_error_set_detail(err, "%s", path);
    return -1;
  }
  if (cmdb_meta_exists(path)) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "Database metadata file already exists");
    return -1;
  }

#ifndef _WIN32
  old_umask = umask(0077);
  umask_set = 1;
#endif

  db = cmdb_calloc(1, sizeof(*db));
  db->path = cmdb_strdup(path);
  cmdb_meta_init_random(&db->meta);

  if (cmdb_meta_create(path, &db->meta, err) != 0) {
#ifndef _WIN32
    (void)umask(old_umask);
#endif
    db_wipe(db);
    free(db);
    return -1;
  }
  meta_created = 1;

  db_created = 1;
  if (db_open_handle(db, 1, err) != 0) {
    goto fail;
  }

  if (db_set_key(db, passphrase, err) != 0) {
    goto fail;
  }
  if (db_verify_key(db, err) != 0) {
    goto fail;
  }
  if (db_apply_pragmas(db, err) != 0) {
    goto fail;
  }
  if (cmdb_schema_create(db, err) != 0) {
    goto fail;
  }
  if (cmdb_schema_seed(db, err) != 0) {
    goto fail;
  }
  if (cmdb_schema_validate(db, err) != 0) {
    goto fail;
  }

  (void)cmdb_file_chmod_0600(path);
  db_chmod_aux_files(path);
  {
    char *meta_path = cmdb_path_meta(path);
    if (meta_path != NULL) {
      (void)cmdb_file_chmod_0600(meta_path);
      free(meta_path);
    }
  }

  *out = db;
#ifndef _WIN32
  if (umask_set) {
    (void)umask(old_umask);
  }
#endif
  return 0;

fail:
#ifndef _WIN32
  if (umask_set) {
    (void)umask(old_umask);
  }
#endif
  db_wipe(db);
  if (db_created) {
    db_remove_aux_files(path);
    (void)cmdb_file_remove(path);
  }
  if (meta_created) {
    (void)cmdb_meta_delete(path);
  }
  free(db);
  return -1;
}

int cmdb_db_open(const char *path, const char *passphrase,
                 cmdb_db_t **out, cmdb_error_t *err) {
  cmdb_db_t *db;

  if (out == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid output argument");
    return -1;
  }
  *out = NULL;
  if (path == NULL || path[0] == '\0' || strlen(path) >= CMDB_MAX_PATH) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid database path");
    return -1;
  }
  if (passphrase == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Passphrase is required");
    return -1;
  }
  if (!cmdb_is_regular_file(path)) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND, "Database file does not exist");
    cmdb_error_set_detail(err, "%s", path);
    return -1;
  }
  if (!cmdb_meta_exists(path)) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND,
                   "Database metadata file is missing");
    return -1;
  }

  db = cmdb_calloc(1, sizeof(*db));
  db->path = cmdb_strdup(path);

  if (cmdb_meta_read(path, &db->meta, err) != 0) {
    db_wipe(db);
    free(db);
    return -1;
  }
  if (db_open_handle(db, 0, err) != 0) {
    db_wipe(db);
    free(db);
    return -1;
  }
  if (db_set_key(db, passphrase, err) != 0) {
    db_wipe(db);
    free(db);
    return -1;
  }
  if (db_verify_key(db, err) != 0) {
    db_wipe(db);
    free(db);
    return -1;
  }
  if (db_apply_pragmas(db, err) != 0) {
    db_wipe(db);
    free(db);
    return -1;
  }
  if (cmdb_schema_migrate(db, err) != 0) {
    db_wipe(db);
    free(db);
    return -1;
  }
  if (cmdb_schema_validate(db, err) != 0) {
    db_wipe(db);
    free(db);
    return -1;
  }

  (void)cmdb_file_chmod_0600(path);
  db_chmod_aux_files(path);
  *out = db;
  return 0;
}

void cmdb_db_close(cmdb_db_t *db) {
  char *path;
  if (db == NULL) {
    return;
  }
  path = db->path != NULL ? cmdb_strdup(db->path) : NULL;
  if (db->handle != NULL) {
    (void)sqlite3_exec(db->handle, "PRAGMA wal_checkpoint(TRUNCATE);",
                       NULL, NULL, NULL);
  }
  db_wipe(db);
  if (path != NULL) {
    (void)cmdb_file_chmod_0600(path);
    free(path);
  }
  free(db);
}

int cmdb_db_backup(cmdb_db_t *db, const char *path, cmdb_error_t *err) {
  sqlite3 *dest = NULL;
  sqlite3_backup *backup = NULL;
  int rc;
  int rc_finish;
  int ok = 0;
  char *meta_path;

  if (db == NULL || db->handle == NULL || path == NULL || path[0] == '\0' ||
      strlen(path) >= CMDB_MAX_PATH) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid backup path");
    return -1;
  }
  if (db->path != NULL && strcmp(path, db->path) == 0) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "The backup must use a different path");
    return -1;
  }
  meta_path = cmdb_path_meta(db->path);
  if (meta_path != NULL && strcmp(path, meta_path) == 0) {
    free(meta_path);
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "The backup must not overwrite the metadata file");
    return -1;
  }
  free(meta_path);
  if (cmdb_path_exists(path)) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "The backup file already exists");
    cmdb_error_set_detail(err, "%s", path);
    return -1;
  }
  meta_path = cmdb_path_meta(path);
  if (meta_path == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot build the metadata path");
    return -1;
  }
  if (cmdb_path_exists(meta_path)) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "The backup metadata file already exists");
    cmdb_error_set_detail(err, "%s", meta_path);
    free(meta_path);
    return -1;
  }
  free(meta_path);

  rc = sqlite3_open_v2(path, &dest,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                         SQLITE_OPEN_NOMUTEX,
                       NULL);
  if (rc != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_IO, "Cannot create the backup file");
    cmdb_error_set_detail(err, "%s",
                          dest != NULL ? sqlite3_errmsg(dest) : "open failed");
    if (dest != NULL) {
      sqlite3_close(dest);
    }
    return -1;
  }
  rc = sqlite3_key(dest, db->key_hex, (int)strlen(db->key_hex));
  if (rc == SQLITE_OK) {
    rc = sqlite3_exec(dest,
                      "PRAGMA cipher_memory_security = ON;"
                      "PRAGMA foreign_keys = ON;",
                      NULL, NULL, NULL);
  }
  if (rc != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot key the backup database");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(dest));
    goto cleanup;
  }
  backup = sqlite3_backup_init(dest, "main", db->handle, "main");
  if (backup == NULL) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot start the database backup");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(dest));
    goto cleanup;
  }
  for (;;) {
    rc = sqlite3_backup_step(backup, 64);
    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
      if (rc == SQLITE_DONE) {
        break;
      }
      if (sqlite3_backup_remaining(backup) == 0) {
        break;
      }
      continue;
    }
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
      sqlite3_sleep(50);
      continue;
    }
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Database backup failed");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(dest));
    goto cleanup;
  }
  rc_finish = sqlite3_backup_finish(backup);
  backup = NULL;
  if (rc_finish != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Cannot finish the database backup");
    cmdb_error_set_detail(err, "%s", sqlite3_errmsg(dest));
    goto cleanup;
  }
  if (sqlite3_close(dest) != SQLITE_OK) {
    dest = NULL;
    cmdb_error_set(err, CMDB_ERR_DATABASE,
                   "Cannot close the backup database");
    goto cleanup;
  }
  dest = NULL;
  if (cmdb_meta_create(path, &db->meta, err) != 0) {
    goto cleanup;
  }
  (void)cmdb_file_chmod_0600(path);
  meta_path = cmdb_path_meta(path);
  if (meta_path != NULL) {
    (void)cmdb_file_chmod_0600(meta_path);
    free(meta_path);
  }
  ok = 1;

cleanup:
  if (backup != NULL) {
    (void)sqlite3_backup_finish(backup);
  }
  if (dest != NULL) {
    sqlite3_close(dest);
  }
  if (!ok) {
    (void)cmdb_file_remove(path);
    (void)cmdb_meta_delete(path);
  }
  return ok ? 0 : -1;
}

sqlite3 *cmdb_db_sqlite(cmdb_db_t *db) {
  return db != NULL ? db->handle : NULL;
}

const char *cmdb_db_path(const cmdb_db_t *db) {
  return db != NULL ? db->path : "";
}

const cmdb_meta_t *cmdb_db_meta(const cmdb_db_t *db) {
  return db != NULL ? &db->meta : NULL;
}

int cmdb_db_exec(cmdb_db_t *db, const char *sql, cmdb_error_t *err) {
  char *message = NULL;
  int rc;

  if (db == NULL || db->handle == NULL || sql == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid database execution");
    return -1;
  }

  rc = sqlite3_exec(db->handle, sql, NULL, NULL, &message);
  if (rc != SQLITE_OK) {
    cmdb_error_set(err, CMDB_ERR_DATABASE, "Database operation failed");
    cmdb_error_set_detail(err, "%s",
                          message != NULL ? message : sqlite3_errmsg(db->handle));
    sqlite3_free(message);
    return -1;
  }
  sqlite3_free(message);
  return 0;
}

int cmdb_db_begin(cmdb_db_t *db, cmdb_error_t *err) {
  return cmdb_db_exec(db, "BEGIN IMMEDIATE;", err);
}

int cmdb_db_commit(cmdb_db_t *db, cmdb_error_t *err) {
  return cmdb_db_exec(db, "COMMIT;", err);
}

int cmdb_db_rollback(cmdb_db_t *db, cmdb_error_t *err) {
  return cmdb_db_exec(db, "ROLLBACK;", err);
}

static int valid_savepoint_name(const char *name) {
  size_t i;
  size_t len;
  if (name == NULL) {
    return 0;
  }
  len = strlen(name);
  if (len == 0 || len > 64) {
    return 0;
  }
  for (i = 0; i < len; i++) {
    char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_')) {
      return 0;
    }
  }
  return 1;
}

int cmdb_db_savepoint_begin(cmdb_db_t *db, const char *name,
                            cmdb_error_t *err) {
  char sql[128];
  if (!valid_savepoint_name(name)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid savepoint name");
    return -1;
  }
  (void)snprintf(sql, sizeof(sql), "SAVEPOINT %s;", name);
  return cmdb_db_exec(db, sql, err);
}

int cmdb_db_savepoint_release(cmdb_db_t *db, const char *name,
                              cmdb_error_t *err) {
  char sql[128];
  if (!valid_savepoint_name(name)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid savepoint name");
    return -1;
  }
  (void)snprintf(sql, sizeof(sql), "RELEASE SAVEPOINT %s;", name);
  return cmdb_db_exec(db, sql, err);
}

int cmdb_db_savepoint_rollback(cmdb_db_t *db, const char *name,
                               cmdb_error_t *err) {
  char sql[192];
  if (!valid_savepoint_name(name)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid savepoint name");
    return -1;
  }
  (void)snprintf(sql, sizeof(sql),
                 "ROLLBACK TO SAVEPOINT %s; RELEASE SAVEPOINT %s;",
                 name, name);
  return cmdb_db_exec(db, sql, err);
}

int cmdb_db_changes(cmdb_db_t *db) {
  if (db == NULL || db->handle == NULL) {
    return 0;
  }
  return sqlite3_changes(db->handle);
}
