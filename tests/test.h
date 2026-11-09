#ifndef CMDB_TEST_H
#define CMDB_TEST_H

#include <stdio.h>

extern int test_failures;

void test_crypto(void);
void test_csv(void);
void test_types(void);
void test_ci(void);
void test_relationships(void);
void test_graph(void);
void test_drawio(void);
void test_import(void);
void test_api(void);

#define TEST_ASSERT(condition) \
  do { \
    if (!(condition)) { \
      (void)fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
      test_failures++; \
    } \
  } while (0)

#define TEST_ASSERT_MSG(condition, message) \
  do { \
    if (!(condition)) { \
      (void)fprintf(stderr, "FAIL %s:%d: %s (%s)\n", \
                    __FILE__, __LINE__, #condition, message); \
      test_failures++; \
    } \
  } while (0)

#define RUN_TEST(fn) \
  do { \
    int before = test_failures; \
    fn(); \
    (void)printf("%-32s %s\n", #fn, \
                 test_failures == before ? "ok" : "FAILED"); \
  } while (0)

#endif /* CMDB_TEST_H */
