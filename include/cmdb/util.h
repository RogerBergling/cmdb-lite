#ifndef CMDB_UTIL_H
#define CMDB_UTIL_H

#include "cmdb/error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *cmdb_malloc(size_t size);
void *cmdb_calloc(size_t count, size_t size);
void *cmdb_realloc(void *ptr, size_t size);
char *cmdb_strdup(const char *s);
char *cmdb_strndup(const char *s, size_t n);
void cmdb_free(void *ptr);

int cmdb_str_copy(char *dst, size_t dst_size, const char *src);
int cmdb_str_copy_n(char *dst, size_t dst_size, const char *src, size_t src_len);
char *cmdb_trim_in_place(char *s);
void cmdb_ascii_lower(char *s);

int cmdb_utf8_valid(const char *s, size_t len);
int cmdb_has_control_chars(const char *s);

int cmdb_path_exists(const char *path);
int cmdb_is_regular_file(const char *path);
int cmdb_path_is_absolute(const char *path);
char *cmdb_path_join(const char *a, const char *b);
char *cmdb_path_meta(const char *db_path);
char *cmdb_executable_dir(void);
char *cmdb_path_to_file_url(const char *path);

int cmdb_file_read_all(const char *path, size_t max_bytes,
                       char **out, size_t *out_len, cmdb_error_t *err);
int cmdb_file_write_atomic(const char *path, const void *data, size_t len,
                           unsigned int mode, cmdb_error_t *err);
int cmdb_file_remove(const char *path);
int cmdb_file_chmod_0600(const char *path);

void cmdb_iso8601_now(char out[21]);
int cmdb_iso8601_valid(const char *s);
int cmdb_parse_int(const char *s, int min_value, int max_value, int *out);
int cmdb_parse_bool(const char *s, int *out);

int cmdb_hex_encode(const unsigned char *in, size_t len,
                    char *out, size_t out_size);
int cmdb_hex_decode(const char *in, unsigned char *out, size_t out_len);

int cmdb_random_uuid(char out[37]);
int cmdb_uuid_valid(const char *s);

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} cmdb_strbuf_t;

void cmdb_strbuf_init(cmdb_strbuf_t *sb);
void cmdb_strbuf_free(cmdb_strbuf_t *sb);
void cmdb_strbuf_append(cmdb_strbuf_t *sb, const char *s);
void cmdb_strbuf_append_n(cmdb_strbuf_t *sb, const char *s, size_t n);
void cmdb_strbuf_appendf(cmdb_strbuf_t *sb, const char *fmt, ...)
  CMDB_PRINTF_LIKE(2, 3);
void cmdb_strbuf_append_xml_escaped(cmdb_strbuf_t *sb, const char *s);
char *cmdb_strbuf_steal(cmdb_strbuf_t *sb);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_UTIL_H */
