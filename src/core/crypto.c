#include "cmdb/crypto.h"

#include "cmdb/limits.h"
#include "cmdb/util.h"

#include <sodium.h>

#include <string.h>

int cmdb_crypto_init(cmdb_error_t *err) {
  if (sodium_init() < 0) {
    cmdb_error_set(err, CMDB_ERR_CRYPTO, "Cannot initialize libsodium");
    return -1;
  }
  return 0;
}

int cmdb_crypto_random(unsigned char *buf, size_t len, cmdb_error_t *err) {
  if (buf == NULL && len > 0) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid random buffer");
    return -1;
  }
  if (sodium_init() < 0) {
    cmdb_error_set(err, CMDB_ERR_CRYPTO, "Cannot initialize libsodium");
    return -1;
  }
  randombytes_buf(buf, len);
  return 0;
}

int cmdb_crypto_derive_key(const char *passphrase,
                           const unsigned char salt[CMDB_KDF_SALT_LEN],
                           unsigned long long opslimit,
                           size_t memlimit,
                           unsigned char out_key[CMDB_KDF_KEY_LEN],
                           cmdb_error_t *err) {
  size_t pass_len;

  if (passphrase == NULL || salt == NULL || out_key == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid key derivation arguments");
    return -1;
  }

  pass_len = strlen(passphrase);
  if (pass_len < CMDB_MIN_PASSPHRASE) {
    cmdb_error_set(err, CMDB_ERR_AUTH,
                   "Passphrase must be at least %d characters",
                   CMDB_MIN_PASSPHRASE);
    return -1;
  }
  if (pass_len > CMDB_MAX_PASSPHRASE) {
    cmdb_error_set(err, CMDB_ERR_AUTH,
                   "Passphrase is too long (maximum %d characters)",
                   CMDB_MAX_PASSPHRASE);
    return -1;
  }
  if (opslimit < CMDB_KDF_OPSLIMIT ||
      memlimit < CMDB_KDF_MEMLIMIT ||
      opslimit > 64u ||
      memlimit > (size_t)1024u * 1024u * 1024u) {
    cmdb_error_set(err, CMDB_ERR_CRYPTO,
                   "Unsafe key derivation parameters");
    return -1;
  }

  if (sodium_init() < 0) {
    cmdb_error_set(err, CMDB_ERR_CRYPTO, "Cannot initialize libsodium");
    return -1;
  }

  if (crypto_pwhash(out_key, CMDB_KDF_KEY_LEN,
                    passphrase, pass_len, salt,
                    opslimit, memlimit,
                    crypto_pwhash_ALG_ARGON2ID13) != 0) {
    cmdb_error_set(err, CMDB_ERR_CRYPTO,
                   "Key derivation failed (out of memory?)");
    return -1;
  }

  return 0;
}

int cmdb_crypto_hash_hex(const void *data, size_t len,
                         char out_hex[65], cmdb_error_t *err) {
  unsigned char hash[crypto_generichash_BYTES];

  if (out_hex == NULL || (data == NULL && len > 0)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid hash arguments");
    return -1;
  }
  if (sodium_init() < 0) {
    cmdb_error_set(err, CMDB_ERR_CRYPTO, "Cannot initialize libsodium");
    return -1;
  }
  if (crypto_generichash(hash, sizeof(hash), (const unsigned char *)data, len,
                         NULL, 0) != 0) {
    cmdb_error_set(err, CMDB_ERR_CRYPTO, "Hash calculation failed");
    return -1;
  }
  if (cmdb_hex_encode(hash, sizeof(hash), out_hex, 65) != 0) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot encode hash");
    return -1;
  }
  return 0;
}

int cmdb_crypto_constant_equal(const void *a, const void *b, size_t len) {
  if (a == NULL || b == NULL) {
    return 0;
  }
  return sodium_memcmp(a, b, len) == 0 ? 1 : 0;
}

void cmdb_crypto_wipe(void *ptr, size_t len) {
  if (ptr != NULL && len > 0) {
    sodium_memzero(ptr, len);
  }
}

int cmdb_key_to_hex(const unsigned char key[CMDB_KDF_KEY_LEN],
                    char out_hex[65], cmdb_error_t *err) {
  if (key == NULL || out_hex == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid key encoding");
    return -1;
  }
  if (cmdb_hex_encode(key, CMDB_KDF_KEY_LEN, out_hex, 65) != 0) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot encode key");
    return -1;
  }
  return 0;
}
