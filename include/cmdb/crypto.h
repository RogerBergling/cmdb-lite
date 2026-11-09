#ifndef CMDB_CRYPTO_H
#define CMDB_CRYPTO_H

#include "cmdb/error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CMDB_KDF_SALT_LEN 16
#define CMDB_KDF_KEY_LEN 32
#define CMDB_KDF_OPSLIMIT 3u
#define CMDB_KDF_MEMLIMIT (256u * 1024u * 1024u)

int cmdb_crypto_init(cmdb_error_t *err);

int cmdb_crypto_random(unsigned char *buf, size_t len, cmdb_error_t *err);

int cmdb_crypto_derive_key(const char *passphrase,
                           const unsigned char salt[CMDB_KDF_SALT_LEN],
                           unsigned long long opslimit,
                           size_t memlimit,
                           unsigned char out_key[CMDB_KDF_KEY_LEN],
                           cmdb_error_t *err);

int cmdb_crypto_hash_hex(const void *data, size_t len,
                         char out_hex[65], cmdb_error_t *err);

int cmdb_crypto_constant_equal(const void *a, const void *b, size_t len);

void cmdb_crypto_wipe(void *ptr, size_t len);

int cmdb_key_to_hex(const unsigned char key[CMDB_KDF_KEY_LEN],
                    char out_hex[65], cmdb_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_CRYPTO_H */
