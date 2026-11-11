#include "test.h"

#include "cmdb/limits.h"
#include "cmdb/settings.h"

#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

void test_settings(void) {
#ifndef _WIN32
  char directory[] = "/tmp/cmdb-settings-test-XXXXXX";
  char expected[CMDB_MAX_PATH];
  char nested[CMDB_MAX_PATH];
  char *actual;
  int written;

  if (mkdtemp(directory) == NULL) {
    TEST_ASSERT_MSG(0, "cannot create temporary settings directory");
    return;
  }
  (void)unsetenv("CMDB_CONFIG_DIR");
  (void)unsetenv("XDG_CONFIG_HOME");
  (void)setenv("HOME", directory, 1);

  actual = cmdb_settings_config_path();
  written = snprintf(expected, sizeof(expected),
                     "%s/.config/cmdb-lite/config.json", directory);
  TEST_ASSERT(written > 0 && (size_t)written < sizeof(expected));
  TEST_ASSERT(actual != NULL);
  if (actual != NULL) {
    TEST_ASSERT(strcmp(actual, expected) == 0);
    free(actual);
  }

  written = snprintf(nested, sizeof(nested), "%s/.config/cmdb-lite", directory);
  if (written > 0 && (size_t)written < sizeof(nested)) {
    (void)rmdir(nested);
  }
  written = snprintf(nested, sizeof(nested), "%s/.config", directory);
  if (written > 0 && (size_t)written < sizeof(nested)) {
    (void)rmdir(nested);
  }
  (void)rmdir(directory);
#else
  (void)0;
#endif
}
