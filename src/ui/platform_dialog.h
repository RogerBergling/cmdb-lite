#ifndef CMDB_PLATFORM_DIALOG_H
#define CMDB_PLATFORM_DIALOG_H

#include "cmdb/error.h"

char *platform_choose_file(int save, const char *title,
                           const char *filter_name, const char *pattern,
                           const char *suggested_name, cmdb_error_t *err);

void platform_configure_webview(void *webview);

#endif /* CMDB_PLATFORM_DIALOG_H */
