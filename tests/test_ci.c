#include "test.h"
#include "test_util.h"

#include "cmdb/ci.h"
#include "cmdb/types.h"

#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

void test_ci(void) {
  char *dir = test_temp_dir();
  char path[4096];
  char enc_path[4096];
  cmdb_db_t *db;
  cmdb_db_t *enc_db;
  cmdb_db_t *wrong_db = NULL;
  cmdb_error_t err;
  cmdb_ci_type_t type;
  cmdb_ci_input_t input;
  cmdb_attr_input_t attrs[2];
  char id[37];
  char id2[37];
  char id3[37];
  char resolved[37];
  cmdb_ci_t ci;
  cmdb_ci_t *list = NULL;
  size_t count = 0;
  size_t total = 0;
  cmdb_ci_filter_t filter;

  TEST_ASSERT(dir != NULL);
  if (dir == NULL) {
    return;
  }
  cmdb_error_clear(&err);
  db = test_create_db(dir, "ci.cmdb", path, sizeof(path));
  TEST_ASSERT(db != NULL);
  if (db == NULL) {
    free(dir);
    return;
  }
  TEST_ASSERT(cmdb_types_find_by_name(db, "Linux Server", &type, &err) == 0);

  attrs[0].key = "ip_address";
  attrs[0].value = "10.0.0.10";
  attrs[1].key = "serial_number";
  attrs[1].value = "SN-1";
  memset(&input, 0, sizeof(input));
  input.name = "app01";
  input.type_id = type.id;
  input.description = "Marker Description";
  input.environment = "Production";
  input.status = "Active";
  input.business_service = "Payments";
  input.technical_owner = "Alice";
  input.support_team = "Linux";
  input.criticality = "High";
  input.hosting_location = "dc1";
  input.external_id = "EXT-1";
  input.attrs = attrs;
  input.attr_count = 2;

  TEST_ASSERT(cmdb_ci_create(db, &input, id, &err) == 0);
  TEST_ASSERT(cmdb_uuid_valid(id));
  TEST_ASSERT(cmdb_ci_get(db, id, &ci, &err) == 0);
  TEST_ASSERT(strcmp(ci.name, "app01") == 0);
  TEST_ASSERT(strcmp(ci.type_name, "Linux Server") == 0);
  TEST_ASSERT(ci.attr_count == 2);
  TEST_ASSERT(strcmp(ci.environment, "Production") == 0);
  cmdb_ci_free(&ci);

  filter.query = "app";
  filter.type_id = NULL;
  filter.limit = 10;
  filter.offset = 0;
  TEST_ASSERT(cmdb_ci_list(db, &filter, &list, &count, &total, &err) == 0);
  TEST_ASSERT(total == 1);
  TEST_ASSERT(count == 1);
  cmdb_ci_free_list(list, count);

  input.description = "Updated description";
  TEST_ASSERT(cmdb_ci_update(db, id, &input, &err) == 0);
  TEST_ASSERT(cmdb_ci_get(db, id, &ci, &err) == 0);
  TEST_ASSERT(strcmp(ci.description, "Updated description") == 0);
  cmdb_ci_free(&ci);

  memset(&input, 0, sizeof(input));
  input.name = "app02";
  input.type_id = type.id;
  input.external_id = "EXT-1";
  TEST_ASSERT(cmdb_ci_create(db, &input, id2, &err) != 0);
  TEST_ASSERT(err.code == CMDB_ERR_CONFLICT);

  input.name = "second";
  input.external_id = "EXT-2";
  TEST_ASSERT(cmdb_ci_create(db, &input, id2, &err) == 0);

  TEST_ASSERT(cmdb_ci_resolve(db, id, resolved, &err) == 0);
  TEST_ASSERT(strcmp(resolved, id) == 0);
  TEST_ASSERT(cmdb_ci_resolve(db, "app01", resolved, &err) == 0);
  TEST_ASSERT(strcmp(resolved, id) == 0);
  TEST_ASSERT(cmdb_ci_resolve(db, "EXT-2", resolved, &err) == 0);
  TEST_ASSERT(strcmp(resolved, id2) == 0);

  input.name = "duplicate";
  input.external_id = NULL;
  TEST_ASSERT(cmdb_ci_create(db, &input, id3, &err) == 0);
  input.name = "duplicate";
  TEST_ASSERT(cmdb_ci_create(db, &input, id3, &err) == 0);
  TEST_ASSERT(cmdb_ci_resolve(db, "duplicate", resolved, &err) != 0);
  TEST_ASSERT(err.code == CMDB_ERR_CONFLICT);

  TEST_ASSERT(cmdb_ci_delete(db, id, &err) == 0);
  TEST_ASSERT(cmdb_ci_get(db, id, &ci, &err) != 0);
  TEST_ASSERT(err.code == CMDB_ERR_NOT_FOUND);
  cmdb_db_close(db);

#ifndef _WIN32
  {
    struct stat st;
    char meta_path[4110];
    (void)snprintf(meta_path, sizeof(meta_path), "%s.meta", path);
    TEST_ASSERT(stat(path, &st) == 0);
    TEST_ASSERT((st.st_mode & 077) == 0);
    TEST_ASSERT(stat(meta_path, &st) == 0);
    TEST_ASSERT((st.st_mode & 077) == 0);
  }
#endif

  cmdb_error_clear(&err);
  enc_db = test_create_db(dir, "enc.cmdb", enc_path, sizeof(enc_path));
  TEST_ASSERT(enc_db != NULL);
  if (enc_db != NULL) {
    char wal_path[4110];
    memset(&input, 0, sizeof(input));
    input.name = "TopSecretMarkerName";
    input.type_id = type.id;
    input.description = "TopSecretMarkerDescription";
    TEST_ASSERT(cmdb_ci_create(enc_db, &input, id, &err) == 0);
    (void)snprintf(wal_path, sizeof(wal_path), "%s-wal", enc_path);
    TEST_ASSERT(!test_file_contains(enc_path, "TopSecretMarkerName"));
    TEST_ASSERT(!test_file_contains(enc_path, "TopSecretMarkerDescription"));
    TEST_ASSERT(!test_file_contains(wal_path, "TopSecretMarkerName"));
    TEST_ASSERT(!test_file_contains(wal_path, "TopSecretMarkerDescription"));
    cmdb_db_close(enc_db);
    TEST_ASSERT(!test_file_contains(enc_path, "TopSecretMarkerName"));
    TEST_ASSERT(!test_file_contains(enc_path, "TopSecretMarkerDescription"));

    cmdb_error_clear(&err);
    TEST_ASSERT(cmdb_db_open(enc_path, "this is the wrong passphrase",
                             &wrong_db, &err) != 0);
    TEST_ASSERT(err.code == CMDB_ERR_AUTH);
    TEST_ASSERT(wrong_db == NULL);
  }

  test_cleanup_db(dir, "ci.cmdb");
  test_cleanup_db(dir, "enc.cmdb");
  free(dir);
}
