#include "test.h"

#include "cmdb/import.h"

#include <string.h>

void test_csv(void) {
  cmdb_error_t err;
  cmdb_csv_table_t table;

  cmdb_error_clear(&err);
  {
    const char *csv = "name,type\n\"a,b\",\"line1\nline2\"\n";
    TEST_ASSERT(cmdb_csv_parse(csv, strlen(csv), ',', &table, &err) == 0);
    TEST_ASSERT(table.column_count == 2);
    TEST_ASSERT(table.row_count == 1);
    TEST_ASSERT(strcmp(table.headers[0], "name") == 0);
    TEST_ASSERT(strcmp(table.headers[1], "type") == 0);
    TEST_ASSERT(strcmp(table.rows[0].cells[0], "a,b") == 0);
    TEST_ASSERT(strcmp(table.rows[0].cells[1], "line1\nline2") == 0);
    cmdb_csv_free(&table);
  }

  cmdb_error_clear(&err);
  {
    const char *csv = "a,b\r\n\"x\"\"y\",2\r\n\r\n";
    TEST_ASSERT(cmdb_csv_parse(csv, strlen(csv), ',', &table, &err) == 0);
    TEST_ASSERT(table.row_count == 1);
    TEST_ASSERT(strcmp(table.rows[0].cells[0], "x\"y") == 0);
    TEST_ASSERT(strcmp(table.rows[0].cells[1], "2") == 0);
    cmdb_csv_free(&table);
  }

  cmdb_error_clear(&err);
  {
    const char *tsv = "ci_name\tci_type\napp01\tLinux Server\n";
    TEST_ASSERT(cmdb_csv_parse(tsv, strlen(tsv), '\t', &table, &err) == 0);
    TEST_ASSERT(table.column_count == 2);
    TEST_ASSERT(table.row_count == 1);
    TEST_ASSERT(strcmp(table.rows[0].cells[1], "Linux Server") == 0);
    cmdb_csv_free(&table);
  }

  cmdb_error_clear(&err);
  {
    const char *csv = "\xEF\xBB\xBF" "CI Name,CI-Type\napp01,Linux Server\n";
    TEST_ASSERT(cmdb_csv_parse(csv, strlen(csv), ',', &table, &err) == 0);
    TEST_ASSERT(strcmp(table.headers[0], "ci_name") == 0);
    TEST_ASSERT(strcmp(table.headers[1], "ci_type") == 0);
    cmdb_csv_free(&table);
  }

  cmdb_error_clear(&err);
  {
    const char *csv = "name,name\nx,y\n";
    TEST_ASSERT(cmdb_csv_parse(csv, strlen(csv), ',', &table, &err) != 0);
    TEST_ASSERT(err.code == CMDB_ERR_VALIDATION);
  }

  cmdb_error_clear(&err);
  {
    const char *csv = "a,b\n1,2,3\n";
    TEST_ASSERT(cmdb_csv_parse(csv, strlen(csv), ',', &table, &err) != 0);
    TEST_ASSERT(err.code == CMDB_ERR_VALIDATION);
  }

  cmdb_error_clear(&err);
  {
    const char *csv = "a,b\n1,\"unterminated\n";
    TEST_ASSERT(cmdb_csv_parse(csv, strlen(csv), ',', &table, &err) != 0);
    TEST_ASSERT(err.code == CMDB_ERR_VALIDATION);
  }

  cmdb_error_clear(&err);
  {
    const char *csv = "a,b\n1\n";
    TEST_ASSERT(cmdb_csv_parse(csv, strlen(csv), ',', &table, &err) == 0);
    TEST_ASSERT(table.row_count == 1);
    TEST_ASSERT(strcmp(table.rows[0].cells[1], "") == 0);
    cmdb_csv_free(&table);
  }
}
