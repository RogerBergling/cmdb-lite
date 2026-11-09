#include "test.h"
#include "test_util.h"

#include "cmdb/crypto.h"
#include "cmdb/meta.h"
#include "cmdb/util.h"

#include <string.h>

void test_crypto(void) {
  cmdb_error_t err;
  unsigned char salt_a[CMDB_KDF_SALT_LEN];
  unsigned char salt_b[CMDB_KDF_SALT_LEN];
  unsigned char key_a[CMDB_KDF_KEY_LEN];
  unsigned char key_b[CMDB_KDF_KEY_LEN];
  unsigned char key_c[CMDB_KDF_KEY_LEN];
  char hash_a[65];
  char hash_b[65];
  char *dir;
  char meta_path[4096];
  char meta_file[4110];
  cmdb_meta_t meta;
  cmdb_meta_t read_meta;
  char *data = NULL;
  size_t data_len = 0;

  cmdb_error_clear(&err);
  TEST_ASSERT(cmdb_crypto_init(&err) == 0);
  memset(salt_a, 0x11, sizeof(salt_a));
  memset(salt_b, 0x22, sizeof(salt_b));

  TEST_ASSERT(cmdb_crypto_derive_key(TEST_PASSPHRASE, salt_a,
                                     CMDB_KDF_OPSLIMIT, CMDB_KDF_MEMLIMIT,
                                     key_a, &err) == 0);
  TEST_ASSERT(cmdb_crypto_derive_key(TEST_PASSPHRASE, salt_a,
                                     CMDB_KDF_OPSLIMIT, CMDB_KDF_MEMLIMIT,
                                     key_b, &err) == 0);
  TEST_ASSERT(memcmp(key_a, key_b, sizeof(key_a)) == 0);
  TEST_ASSERT(cmdb_crypto_derive_key(TEST_PASSPHRASE, salt_b,
                                     CMDB_KDF_OPSLIMIT, CMDB_KDF_MEMLIMIT,
                                     key_c, &err) == 0);
  TEST_ASSERT(memcmp(key_a, key_c, sizeof(key_a)) != 0);

  TEST_ASSERT(cmdb_crypto_hash_hex("abc", 3, hash_a, &err) == 0);
  TEST_ASSERT(cmdb_crypto_hash_hex("abc", 3, hash_b, &err) == 0);
  TEST_ASSERT(strcmp(hash_a, hash_b) == 0);
  TEST_ASSERT(strlen(hash_a) == 64);

  dir = test_temp_dir();
  TEST_ASSERT(dir != NULL);
  if (dir == NULL) {
    return;
  }
  test_path(meta_path, sizeof(meta_path), dir, "crypto-test.cmdb");
  (void)snprintf(meta_file, sizeof(meta_file), "%s.meta", meta_path);

  cmdb_meta_init_random(&meta);
  TEST_ASSERT(cmdb_meta_validate(&meta, &err) == 0);
  TEST_ASSERT(cmdb_meta_create(meta_path, &meta, &err) == 0);
  TEST_ASSERT(cmdb_meta_read(meta_path, &read_meta, &err) == 0);
  TEST_ASSERT(read_meta.version == meta.version);
  TEST_ASSERT(read_meta.opslimit == meta.opslimit);
  TEST_ASSERT(read_meta.memlimit == meta.memlimit);
  TEST_ASSERT(memcmp(read_meta.salt, meta.salt, sizeof(meta.salt)) == 0);

  /* Tamper with the salt and verify the checksum rejects it. */
  TEST_ASSERT(cmdb_file_read_all(meta_file, 4096, &data, &data_len, &err) == 0);
  if (data != NULL) {
    char *salt = strstr(data, "salt=");
    TEST_ASSERT(salt != NULL);
    if (salt != NULL && salt[5] != '\0') {
      salt[5] = salt[5] == '0' ? '1' : '0';
    }
    TEST_ASSERT(cmdb_file_write_atomic(meta_file, data, data_len, 0600,
                                       &err) == 0);
    cmdb_error_clear(&err);
    TEST_ASSERT(cmdb_meta_read(meta_path, &read_meta, &err) != 0);
    TEST_ASSERT(err.code == CMDB_ERR_VALIDATION);
    free(data);
  }

  cmdb_error_clear(&err);
  {
    cmdb_meta_t weak = meta;
    weak.memlimit = 64u * 1024u * 1024u;
    TEST_ASSERT(cmdb_meta_validate(&weak, &err) != 0);
  }

  (void)cmdb_file_remove(meta_file);
  free(dir);
}
