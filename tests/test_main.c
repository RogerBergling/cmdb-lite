#include "test.h"

#include <stdio.h>

int test_failures = 0;

void test_crypto(void);
void test_csv(void);
void test_types(void);
void test_ci(void);
void test_relationships(void);
void test_graph(void);
void test_drawio(void);
void test_import(void);
void test_export(void);
void test_api(void);
void test_scan(void);

int main(void) {
  (void)printf("cmdb-lite tests\n");
  RUN_TEST(test_crypto);
  RUN_TEST(test_csv);
  RUN_TEST(test_types);
  RUN_TEST(test_ci);
  RUN_TEST(test_relationships);
  RUN_TEST(test_graph);
  RUN_TEST(test_drawio);
  RUN_TEST(test_import);
  RUN_TEST(test_export);
  RUN_TEST(test_api);
  RUN_TEST(test_scan);
  RUN_TEST(test_settings);

  (void)printf("\n%d failure(s)\n", test_failures);
  return test_failures == 0 ? 0 : 1;
}
