#include "cmdb/error.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *cmdb_status_name(cmdb_status_t status) {
  switch (status) {
    case CMDB_OK: return "ok";
    case CMDB_ERR_INVALID_ARGUMENT: return "invalid_argument";
    case CMDB_ERR_IO: return "io";
    case CMDB_ERR_CRYPTO: return "crypto";
    case CMDB_ERR_DATABASE: return "database";
    case CMDB_ERR_NOT_FOUND: return "not_found";
    case CMDB_ERR_CONFLICT: return "conflict";
    case CMDB_ERR_VALIDATION: return "validation";
    case CMDB_ERR_AUTH: return "auth";
    case CMDB_ERR_LIMIT: return "limit";
    case CMDB_ERR_CANCELLED: return "cancelled";
    case CMDB_ERR_INTERNAL: return "internal";
    default: return "internal";
  }
}

void cmdb_error_clear(cmdb_error_t *err) {
  if (err == NULL) {
    return;
  }
  memset(err, 0, sizeof(*err));
  err->code = CMDB_OK;
}

void cmdb_error_set(cmdb_error_t *err, cmdb_status_t code,
                    const char *fmt, ...) {
  va_list args;

  if (err == NULL) {
    return;
  }

  memset(err, 0, sizeof(*err));
  err->code = code;

  if (fmt == NULL) {
    return;
  }

  va_start(args, fmt);
  (void)vsnprintf(err->message, sizeof(err->message), fmt, args);
  va_end(args);
  err->message[sizeof(err->message) - 1] = '\0';
}

void cmdb_error_set_detail(cmdb_error_t *err, const char *fmt, ...) {
  va_list args;

  if (err == NULL || fmt == NULL) {
    return;
  }

  va_start(args, fmt);
  (void)vsnprintf(err->detail, sizeof(err->detail), fmt, args);
  va_end(args);
  err->detail[sizeof(err->detail) - 1] = '\0';
}
