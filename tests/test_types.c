#include "test.h"
#include "test_util.h"

#include "cmdb/types.h"
#include "cmdb/categories.h"

#include <string.h>

void test_types(void) {
  char *dir = test_temp_dir();
  char path[4096];
  cmdb_db_t *db;
  cmdb_error_t err;
  cmdb_ci_type_t *types = NULL;
  size_t count = 0;
  cmdb_ci_type_t linux_type;
  cmdb_ci_type_t service_type;
  cmdb_ci_type_t new_type;
  cmdb_type_field_t *fields = NULL;
  cmdb_type_field_t got_field;
  size_t field_count = 0;
  cmdb_type_field_input_t input;
  cmdb_category_t category;
  char category_id[37];
  char type_id[37];
  char field_id[37];
  char duplicate_id[37];

  TEST_ASSERT(dir != NULL);
  if (dir == NULL) {
    return;
  }
  cmdb_error_clear(&err);
  db = test_create_db(dir, "types.cmdb", path, sizeof(path));
  TEST_ASSERT(db != NULL);
  if (db == NULL) {
    free(dir);
    return;
  }

  TEST_ASSERT(cmdb_types_list(db, &types, &count, &err) == 0);
  TEST_ASSERT(count >= 12);
  cmdb_types_free(types);

  TEST_ASSERT(cmdb_types_find_by_name(db, "Linux Server", &linux_type, &err) == 0);
  TEST_ASSERT(strcmp(linux_type.name, "Linux Server") == 0);
  TEST_ASSERT(strcmp(linux_type.category, "Compute") == 0);

  TEST_ASSERT(cmdb_types_find_by_name(db, "Service", &service_type, &err) == 0);
  TEST_ASSERT(strcmp(service_type.category, "Services") == 0);
  TEST_ASSERT(cmdb_type_fields_list(db, service_type.id, &fields, &field_count,
                                    &err) == 0);
  {
    size_t i;
    int seen_type = 0;
    int seen_availability = 0;
    for (i = 0; i < field_count; i++) {
      if (strcmp(fields[i].key, "service_type") == 0) {
        seen_type = 1;
        TEST_ASSERT(strstr(fields[i].options_json, "DNS") != NULL);
      }
      if (strcmp(fields[i].key, "availability_mode") == 0) {
        seen_availability = 1;
      }
    }
    TEST_ASSERT(seen_type);
    TEST_ASSERT(seen_availability);
  }
  cmdb_type_fields_free(fields);
  fields = NULL;

  TEST_ASSERT(cmdb_categories_create(db, "Test", "Test category", "#123456", 10,
                                      category_id, &err) == 0);
  TEST_ASSERT(cmdb_categories_get(db, category_id, &category, &err) == 0);
  TEST_ASSERT(strcmp(category.name, "Test") == 0);

  TEST_ASSERT(cmdb_types_create(db, "Test Type", "A test type", "test", "#123456",
                                category_id, type_id, &err) == 0);
  TEST_ASSERT(cmdb_types_get(db, type_id, &new_type, &err) == 0);
  TEST_ASSERT(strcmp(new_type.name, "Test Type") == 0);
  TEST_ASSERT(strcmp(new_type.category, "Test") == 0);
  TEST_ASSERT(strcmp(new_type.category_id, category_id) == 0);
  TEST_ASSERT(cmdb_types_update(db, type_id, "Test Type", "A test type",
                                "test", "#123456", category_id,
                                &err) == 0);
  TEST_ASSERT(cmdb_types_get(db, type_id, &new_type, &err) == 0);
  TEST_ASSERT(strcmp(new_type.category, "Test") == 0);

  memset(&input, 0, sizeof(input));
  input.key = "ip_address";
  input.label = "IP Address";
  input.field_type = "text";
  input.required = 1;
  input.options_json = "[]";
  input.sort_order = 10;
  TEST_ASSERT(cmdb_type_field_add(db, type_id, &input, field_id, &err) == 0);
  TEST_ASSERT(cmdb_type_fields_list(db, type_id, &fields, &field_count, &err) == 0);
  TEST_ASSERT(field_count == 1);
  TEST_ASSERT(strcmp(fields[0].key, "ip_address") == 0);
  TEST_ASSERT(fields[0].required == 1);
  cmdb_type_fields_free(fields);

  input.label = "IP";
  TEST_ASSERT(cmdb_type_field_update(db, field_id, &input, &err) == 0);
  TEST_ASSERT(cmdb_type_field_get(db, field_id, &got_field, &err) == 0);
  TEST_ASSERT(strcmp(got_field.label, "IP") == 0);

  TEST_ASSERT(cmdb_type_field_add(db, type_id, &input, duplicate_id, &err) != 0);
  TEST_ASSERT(err.code == CMDB_ERR_CONFLICT);

  TEST_ASSERT(cmdb_type_field_delete(db, field_id, &err) == 0);
  TEST_ASSERT(cmdb_types_delete(db, type_id, &err) == 0);
  TEST_ASSERT(cmdb_types_get(db, type_id, &new_type, &err) != 0);
  TEST_ASSERT(err.code == CMDB_ERR_NOT_FOUND);

  cmdb_db_close(db);
  test_cleanup_db(dir, "types.cmdb");
  free(dir);
}
