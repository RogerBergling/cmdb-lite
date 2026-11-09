#ifndef CMDB_META_H
#define CMDB_META_H

#include "cmdb/crypto.h"
#include "cmdb/error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CMDB_META_VERSION 1u

typedef struct {
  unsigned int version;
  unsigned long long opslimit;
  size_t memlimit;
  unsigned char salt[CMDB_KDF_SALT_LEN];
} cmdb_meta_t;

void cmdb_meta_init_random(cmdb_meta_t *meta);

int cmdb_meta_validate(const cmdb_meta_t *meta, cmdb_error_t *err);

int cmdb_meta_create(const char *db_path, const cmdb_meta_t *meta,
                     cmdb_error_t *err);

int cmdb_meta_read(const char *db_path, cmdb_meta_t *meta,
                   cmdb_error_t *err);

int cmdb_meta_exists(const char *db_path);
int cmdb_meta_delete(const char *db_path);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_META_H */
