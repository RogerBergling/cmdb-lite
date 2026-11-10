#ifndef CMDB_ERROR_H
#define CMDB_ERROR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CMDB_PRINTF_LIKE(fmt_index, arg_index) \
  __attribute__((format(printf, fmt_index, arg_index)))
#else
#define CMDB_PRINTF_LIKE(fmt_index, arg_index)
#endif

typedef enum {
  CMDB_OK = 0,
  CMDB_ERR_INVALID_ARGUMENT,
  CMDB_ERR_IO,
  CMDB_ERR_CRYPTO,
  CMDB_ERR_DATABASE,
  CMDB_ERR_NOT_FOUND,
  CMDB_ERR_CONFLICT,
  CMDB_ERR_VALIDATION,
  CMDB_ERR_AUTH,
  CMDB_ERR_LIMIT,
  CMDB_ERR_CANCELLED,
  CMDB_ERR_INTERNAL
} cmdb_status_t;

typedef struct {
  cmdb_status_t code;
  int line;
  char message[512];
  char detail[1024];
} cmdb_error_t;

const char *cmdb_status_name(cmdb_status_t status);

void cmdb_error_clear(cmdb_error_t *err);

void cmdb_error_set(cmdb_error_t *err, cmdb_status_t code,
                    const char *fmt, ...) CMDB_PRINTF_LIKE(3, 4);

void cmdb_error_set_detail(cmdb_error_t *err,
                           const char *fmt, ...) CMDB_PRINTF_LIKE(2, 3);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_ERROR_H */
