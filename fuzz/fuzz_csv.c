#include "cmdb/import.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  cmdb_csv_table_t table;
  cmdb_error_t err;

  cmdb_error_clear(&err);
  if (cmdb_csv_parse((const char *)data, size, ',', &table, &err) == 0) {
    cmdb_csv_free(&table);
  }
  return 0;
}
