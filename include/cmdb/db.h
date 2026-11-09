#ifndef CMDB_DB_H
#define CMDB_DB_H

#include "cmdb/error.h"
#include "cmdb/meta.h"

#include <sqlcipher/sqlite3.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cmdb_db cmdb_db_t;

int cmdb_db_create(const char *path, const char *passphrase,
                   cmdb_db_t **out, cmdb_error_t *err);

int cmdb_db_open(const char *path, const char *passphrase,
                 cmdb_db_t **out, cmdb_error_t *err);

void cmdb_db_close(cmdb_db_t *db);

sqlite3 *cmdb_db_sqlite(cmdb_db_t *db);
const char *cmdb_db_path(const cmdb_db_t *db);
const cmdb_meta_t *cmdb_db_meta(const cmdb_db_t *db);

int cmdb_db_exec(cmdb_db_t *db, const char *sql, cmdb_error_t *err);
int cmdb_db_begin(cmdb_db_t *db, cmdb_error_t *err);
int cmdb_db_commit(cmdb_db_t *db, cmdb_error_t *err);
int cmdb_db_rollback(cmdb_db_t *db, cmdb_error_t *err);

int cmdb_db_savepoint_begin(cmdb_db_t *db, const char *name,
                            cmdb_error_t *err);
int cmdb_db_savepoint_release(cmdb_db_t *db, const char *name,
                              cmdb_error_t *err);
int cmdb_db_savepoint_rollback(cmdb_db_t *db, const char *name,
                               cmdb_error_t *err);

int cmdb_db_changes(cmdb_db_t *db);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_DB_H */
