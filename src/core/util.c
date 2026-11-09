#include "cmdb/util.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <wchar.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#ifndef getpid
#define getpid _getpid
#endif
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif
#define CMDB_STAT_TYPE struct _stat64
#define CMDB_STAT_FUNC _stat64

static wchar_t *utf8_to_wide_path(const char *path) {
  int length;
  wchar_t *wide;
  size_t i;

  if (path == NULL) {
    return NULL;
  }
  length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                               NULL, 0);
  if (length <= 0) {
    return NULL;
  }
  wide = cmdb_calloc((size_t)length, sizeof(wchar_t));
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                          wide, length) != length) {
    free(wide);
    return NULL;
  }
  for (i = 0; wide[i] != L'\0'; i++) {
    if (wide[i] == L'/') {
      wide[i] = L'\\';
    }
  }
  return wide;
}

static char *wide_to_utf8_path(const wchar_t *wide) {
  int length;
  char *utf8;

  if (wide == NULL) {
    return NULL;
  }
  length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
  if (length <= 0) {
    return NULL;
  }
  utf8 = cmdb_calloc((size_t)length, sizeof(char));
  if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, length,
                          NULL, NULL) != length) {
    free(utf8);
    return NULL;
  }
  return utf8;
}
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#else
#include <fcntl.h>
#include <unistd.h>
#define CMDB_STAT_TYPE struct stat
#define CMDB_STAT_FUNC stat
#endif

void *cmdb_malloc(size_t size) {
  void *ptr;
  if (size == 0) {
    size = 1;
  }
  ptr = malloc(size);
  if (ptr == NULL) {
    (void)fprintf(stderr, "cmdb-lite: out of memory\n");
    abort();
  }
  return ptr;
}

void *cmdb_calloc(size_t count, size_t size) {
  void *ptr;
  if (count == 0 || size == 0) {
    count = 1;
    size = 1;
  }
  if (count > SIZE_MAX / size) {
    (void)fprintf(stderr, "cmdb-lite: allocation overflow\n");
    abort();
  }
  ptr = calloc(count, size);
  if (ptr == NULL) {
    (void)fprintf(stderr, "cmdb-lite: out of memory\n");
    abort();
  }
  return ptr;
}

void *cmdb_realloc(void *ptr, size_t size) {
  void *next;
  if (size == 0) {
    size = 1;
  }
  next = realloc(ptr, size);
  if (next == NULL) {
    (void)fprintf(stderr, "cmdb-lite: out of memory\n");
    abort();
  }
  return next;
}

char *cmdb_strdup(const char *s) {
  size_t len;
  char *copy;
  if (s == NULL) {
    return NULL;
  }
  len = strlen(s);
  copy = cmdb_malloc(len + 1);
  memcpy(copy, s, len + 1);
  return copy;
}

char *cmdb_strndup(const char *s, size_t n) {
  size_t len = 0;
  char *copy;
  if (s == NULL) {
    return NULL;
  }
  while (len < n && s[len] != '\0') {
    len++;
  }
  copy = cmdb_malloc(len + 1);
  memcpy(copy, s, len);
  copy[len] = '\0';
  return copy;
}

void cmdb_free(void *ptr) {
  free(ptr);
}

int cmdb_str_copy(char *dst, size_t dst_size, const char *src) {
  size_t len;
  if (dst == NULL || dst_size == 0) {
    return -1;
  }
  if (src == NULL) {
    dst[0] = '\0';
    return 0;
  }
  len = strlen(src);
  if (len >= dst_size) {
    dst[0] = '\0';
    return -1;
  }
  memcpy(dst, src, len + 1);
  return 0;
}

int cmdb_str_copy_n(char *dst, size_t dst_size, const char *src,
                    size_t src_len) {
  if (dst == NULL || dst_size == 0) {
    return -1;
  }
  if (src == NULL) {
    dst[0] = '\0';
    return 0;
  }
  if (src_len >= dst_size) {
    dst[0] = '\0';
    return -1;
  }
  memcpy(dst, src, src_len);
  dst[src_len] = '\0';
  return 0;
}

char *cmdb_trim_in_place(char *s) {
  char *start;
  size_t len;

  if (s == NULL) {
    return NULL;
  }

  start = s;
  while (*start != '\0' && isspace((unsigned char)*start)) {
    start++;
  }

  len = strlen(start);
  if (start != s) {
    memmove(s, start, len + 1);
  }

  while (len > 0 && isspace((unsigned char)s[len - 1])) {
    s[len - 1] = '\0';
    len--;
  }

  return s;
}

void cmdb_ascii_lower(char *s) {
  if (s == NULL) {
    return;
  }
  while (*s != '\0') {
    if (*s >= 'A' && *s <= 'Z') {
      *s = (char)(*s - 'A' + 'a');
    }
    s++;
  }
}

int cmdb_utf8_valid(const char *s, size_t len) {
  size_t i = 0;

  if (s == NULL) {
    return 0;
  }

  while (i < len) {
    unsigned char c = (unsigned char)s[i];
    uint32_t cp;
    size_t need;
    size_t j;

    if (c == 0) {
      return 0;
    }
    if (c < 0x80) {
      i++;
      continue;
    }

    if ((c & 0xE0) == 0xC0) {
      need = 1;
      cp = (uint32_t)(c & 0x1F);
      if (cp < 2) {
        return 0;
      }
    } else if ((c & 0xF0) == 0xE0) {
      need = 2;
      cp = (uint32_t)(c & 0x0F);
    } else if ((c & 0xF8) == 0xF0) {
      need = 3;
      cp = (uint32_t)(c & 0x07);
    } else {
      return 0;
    }

    if (i + need >= len) {
      return 0;
    }

    for (j = 1; j <= need; j++) {
      unsigned char cc = (unsigned char)s[i + j];
      if ((cc & 0xC0) != 0x80) {
        return 0;
      }
      cp = (cp << 6) | (uint32_t)(cc & 0x3F);
    }

    if (need == 2 && cp < 0x800) {
      return 0;
    }
    if (need == 3 && cp < 0x10000) {
      return 0;
    }
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      return 0;
    }

    i += need + 1;
  }

  return 1;
}

int cmdb_has_control_chars(const char *s) {
  if (s == NULL) {
    return 0;
  }
  while (*s != '\0') {
    unsigned char c = (unsigned char)*s;
    if (c < 0x20 || c == 0x7F) {
      return 1;
    }
    s++;
  }
  return 0;
}

int cmdb_path_exists(const char *path) {
#ifdef _WIN32
  wchar_t *wide;
  DWORD attributes;
  if (path == NULL || path[0] == '\0') {
    return 0;
  }
  wide = utf8_to_wide_path(path);
  if (wide == NULL) {
    return 0;
  }
  attributes = GetFileAttributesW(wide);
  free(wide);
  return attributes != INVALID_FILE_ATTRIBUTES;
#else
  CMDB_STAT_TYPE st;
  if (path == NULL || path[0] == '\0') {
    return 0;
  }
  return CMDB_STAT_FUNC(path, &st) == 0;
#endif
}

int cmdb_is_regular_file(const char *path) {
#ifdef _WIN32
  wchar_t *wide;
  DWORD attributes;
  if (path == NULL || path[0] == '\0') {
    return 0;
  }
  wide = utf8_to_wide_path(path);
  if (wide == NULL) {
    return 0;
  }
  attributes = GetFileAttributesW(wide);
  free(wide);
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    return 0;
  }
  return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ? 1 : 0;
#else
  CMDB_STAT_TYPE st;
  if (path == NULL || path[0] == '\0') {
    return 0;
  }
  if (CMDB_STAT_FUNC(path, &st) != 0) {
    return 0;
  }
  return S_ISREG(st.st_mode) ? 1 : 0;
#endif
}

int cmdb_path_is_absolute(const char *path) {
  if (path == NULL || path[0] == '\0') {
    return 0;
  }
#ifdef _WIN32
  if ((path[0] == '\\' && path[1] == '\\') ||
      (isalpha((unsigned char)path[0]) && path[1] == ':')) {
    return 1;
  }
  return path[0] == '\\' || path[0] == '/';
#else
  return path[0] == '/';
#endif
}

char *cmdb_path_join(const char *a, const char *b) {
  size_t alen;
  size_t blen;
  int need_sep;
  char *out;

  if (a == NULL || a[0] == '\0') {
    return cmdb_strdup(b == NULL ? "" : b);
  }
  if (b == NULL || b[0] == '\0') {
    return cmdb_strdup(a);
  }
  if (cmdb_path_is_absolute(b) ||
      (b[0] == '\\' && b[1] == '\\')) {
    return cmdb_strdup(b);
  }

  alen = strlen(a);
  blen = strlen(b);
  need_sep = !(a[alen - 1] == '/' || a[alen - 1] == '\\');

  out = cmdb_malloc(alen + blen + (need_sep ? 2 : 1));
  memcpy(out, a, alen);
  if (need_sep) {
    out[alen] = '/';
    alen++;
  }
  memcpy(out + alen, b, blen + 1);
  return out;
}

char *cmdb_path_meta(const char *db_path) {
  size_t len;
  char *out;
  if (db_path == NULL) {
    return NULL;
  }
  len = strlen(db_path);
  out = cmdb_malloc(len + 6);
  memcpy(out, db_path, len);
  memcpy(out + len, ".meta", 6);
  return out;
}

char *cmdb_executable_dir(void) {
#ifdef _WIN32
  wchar_t wide_buffer[32768];
  DWORD len = GetModuleFileNameW(NULL, wide_buffer,
                                 (DWORD)(sizeof(wide_buffer) /
                                         sizeof(wide_buffer[0])));
  wchar_t *slash;
  char *utf8;
  if (len == 0 || len >= (sizeof(wide_buffer) / sizeof(wide_buffer[0]))) {
    return NULL;
  }
  slash = wcsrchr(wide_buffer, L'\\');
  if (slash == NULL) {
    slash = wcsrchr(wide_buffer, L'/');
  }
  if (slash == NULL) {
    return cmdb_strdup(".");
  }
  *slash = L'\0';
  utf8 = wide_to_utf8_path(wide_buffer);
  return utf8;
#else
  char buffer[4096];
  ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  char *slash;
  if (len <= 0) {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
      return NULL;
    }
    return cmdb_strdup(cwd);
  }
  buffer[len] = '\0';
  slash = strrchr(buffer, '/');
  if (slash == NULL) {
    return cmdb_strdup(".");
  }
  *slash = '\0';
  return cmdb_strdup(buffer);
#endif
}

char *cmdb_path_to_file_url(const char *path) {
  cmdb_strbuf_t sb;
  const unsigned char *p;
  int windows_path;

  if (path == NULL || path[0] == '\0') {
    return NULL;
  }

  windows_path = isalpha((unsigned char)path[0]) && path[1] == ':';
  cmdb_strbuf_init(&sb);
  cmdb_strbuf_append(&sb, "file://");

  if (windows_path) {
    cmdb_strbuf_append(&sb, "/");
  } else if (path[0] != '/') {
    cmdb_strbuf_append(&sb, "/");
  }

  p = (const unsigned char *)path;
  while (*p != '\0') {
    unsigned char c = *p;
    if (c == '\\') {
      cmdb_strbuf_append(&sb, "/");
    } else if (isalnum(c) || c == '-' || c == '.' || c == '_' ||
               c == '~' || c == '/' || c == ':') {
      char one[2];
      one[0] = (char)c;
      one[1] = '\0';
      cmdb_strbuf_append(&sb, one);
    } else {
      char encoded[4];
      (void)snprintf(encoded, sizeof(encoded), "%%%02X", (unsigned int)c);
      cmdb_strbuf_append(&sb, encoded);
    }
    p++;
  }

  return cmdb_strbuf_steal(&sb);
}

int cmdb_file_read_all(const char *path, size_t max_bytes,
                       char **out, size_t *out_len, cmdb_error_t *err) {
  FILE *file;
  uint64_t file_size;
  char *buffer;
  size_t capacity;
  size_t length = 0;

  if (path == NULL || out == NULL || out_len == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid file read arguments");
    return -1;
  }
  *out = NULL;
  *out_len = 0;

#ifdef _WIN32
  {
    wchar_t *wide = utf8_to_wide_path(path);
    struct _stat64 st;
    if (wide == NULL) {
      cmdb_error_set(err, CMDB_ERR_IO, "Invalid UTF-8 path");
      return -1;
    }
    if (_wstat64(wide, &st) != 0) {
      cmdb_error_set(err, CMDB_ERR_IO, "Cannot stat file");
      cmdb_error_set_detail(err, "%s: %s", path, strerror(errno));
      free(wide);
      return -1;
    }
    if ((st.st_mode & _S_IFMT) != _S_IFREG) {
      cmdb_error_set(err, CMDB_ERR_IO, "Not a regular file");
      cmdb_error_set_detail(err, "%s", path);
      free(wide);
      return -1;
    }
    if (st.st_size < 0 || (uint64_t)st.st_size > (uint64_t)max_bytes) {
      cmdb_error_set(err, CMDB_ERR_LIMIT, "File is too large");
      cmdb_error_set_detail(err, "%s (limit %zu bytes)", path, max_bytes);
      free(wide);
      return -1;
    }
    file_size = (uint64_t)st.st_size;
    file = _wfopen(wide, L"rb");
    free(wide);
  }
#else
  {
    struct stat st;
    if (stat(path, &st) != 0) {
      cmdb_error_set(err, CMDB_ERR_IO, "Cannot stat file");
      cmdb_error_set_detail(err, "%s: %s", path, strerror(errno));
      return -1;
    }
    if (!S_ISREG(st.st_mode)) {
      cmdb_error_set(err, CMDB_ERR_IO, "Not a regular file");
      cmdb_error_set_detail(err, "%s", path);
      return -1;
    }
    if (st.st_size < 0 || (uint64_t)st.st_size > (uint64_t)max_bytes) {
      cmdb_error_set(err, CMDB_ERR_LIMIT, "File is too large");
      cmdb_error_set_detail(err, "%s (limit %zu bytes)", path, max_bytes);
      return -1;
    }
    file_size = (uint64_t)st.st_size;
    file = fopen(path, "rb");
  }
#endif

  if (file == NULL) {
    cmdb_error_set(err, CMDB_ERR_IO, "Cannot open file");
    cmdb_error_set_detail(err, "%s: %s", path, strerror(errno));
    return -1;
  }

  capacity = (size_t)file_size + 1;
  if (capacity < 4096) {
    capacity = 4096;
  }
  buffer = cmdb_malloc(capacity);

  for (;;) {
    size_t got;
    if (length + 1 >= capacity) {
      size_t next;
      if (capacity > max_bytes) {
        break;
      }
      next = capacity * 2;
      if (next > max_bytes + 1) {
        next = max_bytes + 1;
      }
      if (next <= capacity) {
        break;
      }
      capacity = next;
      buffer = cmdb_realloc(buffer, capacity);
    }
    got = fread(buffer + length, 1, capacity - length - 1, file);
    length += got;
    if (length > max_bytes) {
      free(buffer);
      (void)fclose(file);
      cmdb_error_set(err, CMDB_ERR_LIMIT, "File is too large");
      cmdb_error_set_detail(err, "%s (limit %zu bytes)", path, max_bytes);
      return -1;
    }
    if (got == 0) {
      if (ferror(file)) {
        free(buffer);
        (void)fclose(file);
        cmdb_error_set(err, CMDB_ERR_IO, "Cannot read file");
        cmdb_error_set_detail(err, "%s", path);
        return -1;
      }
      break;
    }
  }

  (void)fclose(file);
  buffer[length] = '\0';
  *out = buffer;
  *out_len = length;
  return 0;
}

int cmdb_file_write_atomic(const char *path, const void *data, size_t len,
                           unsigned int mode, cmdb_error_t *err) {
  char *temp;
  size_t temp_len;
  FILE *file = NULL;
  int ok = 0;
  const char *error_path = NULL;

  if (path == NULL || (data == NULL && len > 0)) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid file write arguments");
    return -1;
  }

  temp_len = strlen(path) + 64;
  temp = cmdb_malloc(temp_len);
  (void)snprintf(temp, temp_len, "%s.tmp.%lu.0", path,
                 (unsigned long)getpid());

#ifdef _WIN32
  {
    wchar_t *wide_temp = utf8_to_wide_path(temp);
    wchar_t *wide_path = utf8_to_wide_path(path);
    (void)mode;
    if (wide_temp == NULL || wide_path == NULL) {
      free(wide_temp);
      free(wide_path);
      free(temp);
      cmdb_error_set(err, CMDB_ERR_IO, "Invalid UTF-8 path");
      return -1;
    }
    file = _wfopen(wide_temp, L"wb");
    if (file == NULL) {
      error_path = temp;
      cmdb_error_set(err, CMDB_ERR_IO, "Cannot create temporary file");
      cmdb_error_set_detail(err, "%s: %s", temp, strerror(errno));
      DeleteFileW(wide_temp);
      free(wide_temp);
      free(wide_path);
      free(temp);
      return -1;
    }
    if (len > 0 && fwrite(data, 1, len, file) != len) {
      error_path = path;
      goto fail;
    }
    if (fflush(file) != 0 || _commit(_fileno(file)) != 0) {
      error_path = path;
      goto fail;
    }
    if (fclose(file) != 0) {
      file = NULL;
      error_path = path;
      goto fail;
    }
    file = NULL;
    if (!MoveFileExW(wide_temp, wide_path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      error_path = path;
      DeleteFileW(wide_temp);
      goto fail;
    }
    free(wide_temp);
    free(wide_path);
    ok = 1;
    goto done;
fail:
    if (file != NULL) {
      (void)fclose(file);
      file = NULL;
    }
    DeleteFileW(wide_temp);
    free(wide_temp);
    free(wide_path);
    cmdb_error_set(err, CMDB_ERR_IO, "Cannot write file");
    cmdb_error_set_detail(err, "%s", error_path != NULL ? error_path : path);
  }
#else
  {
    int fd = open(temp, O_WRONLY | O_CREAT | O_EXCL, (mode_t)mode);
    if (fd < 0) {
      cmdb_error_set(err, CMDB_ERR_IO, "Cannot create temporary file");
      cmdb_error_set_detail(err, "%s: %s", temp, strerror(errno));
      free(temp);
      return -1;
    }
    file = fdopen(fd, "wb");
    if (file == NULL) {
      (void)close(fd);
      (void)unlink(temp);
      cmdb_error_set(err, CMDB_ERR_IO, "Cannot open temporary file");
      cmdb_error_set_detail(err, "%s: %s", temp, strerror(errno));
      free(temp);
      return -1;
    }
    if (len > 0 && fwrite(data, 1, len, file) != len) {
      error_path = path;
      goto fail;
    }
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) {
      error_path = path;
      goto fail;
    }
    if (fclose(file) != 0) {
      file = NULL;
      error_path = path;
      goto fail;
    }
    file = NULL;
    if (rename(temp, path) != 0) {
      error_path = path;
      (void)unlink(temp);
      goto fail;
    }
    (void)chmod(path, (mode_t)mode);
    ok = 1;
    goto done;
fail:
    if (file != NULL) {
      (void)fclose(file);
      file = NULL;
    }
    (void)unlink(temp);
    cmdb_error_set(err, CMDB_ERR_IO, "Cannot write file");
    cmdb_error_set_detail(err, "%s: %s",
                          error_path != NULL ? error_path : path,
                          strerror(errno));
  }
#endif

done:
  free(temp);
  return ok ? 0 : -1;
}

int cmdb_file_remove(const char *path) {
#ifdef _WIN32
  wchar_t *wide;
  if (path == NULL) {
    return -1;
  }
  wide = utf8_to_wide_path(path);
  if (wide == NULL) {
    return -1;
  }
  if (DeleteFileW(wide)) {
    free(wide);
    return 0;
  }
  {
    DWORD code = GetLastError();
    free(wide);
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
      return 0;
    }
  }
  return -1;
#else
  if (path == NULL) {
    return -1;
  }
  if (remove(path) == 0) {
    return 0;
  }
  if (errno == ENOENT) {
    return 0;
  }
  return -1;
#endif
}

int cmdb_file_chmod_0600(const char *path) {
#ifdef _WIN32
  (void)path;
  return 0;
#else
  if (path == NULL) {
    return -1;
  }
  return chmod(path, 0600);
#endif
}

void cmdb_iso8601_now(char out[21]) {
  time_t now;
  struct tm tm_value;

  if (out == NULL) {
    return;
  }

  now = time(NULL);
#ifdef _WIN32
  if (gmtime_s(&tm_value, &now) != 0) {
    out[0] = '\0';
    return;
  }
#else
  if (gmtime_r(&now, &tm_value) == NULL) {
    out[0] = '\0';
    return;
  }
#endif
  if (strftime(out, 21, "%Y-%m-%dT%H:%M:%SZ", &tm_value) == 0) {
    out[0] = '\0';
  }
}

static int is_leap_year(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int cmdb_iso8601_valid(const char *s) {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  int days_in_month;
  size_t i;

  static const int month_days[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };

  if (s == NULL || strlen(s) != 20) {
    return 0;
  }
  for (i = 0; i < 20; i++) {
    if (i == 4 || i == 7) {
      if (s[i] != '-') {
        return 0;
      }
    } else if (i == 10) {
      if (s[i] != 'T') {
        return 0;
      }
    } else if (i == 13 || i == 16) {
      if (s[i] != ':') {
        return 0;
      }
    } else if (i == 19) {
      if (s[i] != 'Z') {
        return 0;
      }
    } else if (!isdigit((unsigned char)s[i])) {
      return 0;
    }
  }

  if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2dZ",
             &year, &month, &day, &hour, &minute, &second) != 6) {
    return 0;
  }

  if (year < 1970 || year > 9999 || month < 1 || month > 12 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) {
    return 0;
  }

  days_in_month = month_days[month - 1];
  if (month == 2 && is_leap_year(year)) {
    days_in_month = 29;
  }
  return day >= 1 && day <= days_in_month;
}

int cmdb_parse_int(const char *s, int min_value, int max_value, int *out) {
  char *end = NULL;
  long value;

  if (s == NULL || out == NULL || s[0] == '\0') {
    return -1;
  }
  errno = 0;
  value = strtol(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0') {
    return -1;
  }
  if (value < min_value || value > max_value) {
    return -1;
  }
  *out = (int)value;
  return 0;
}

int cmdb_parse_bool(const char *s, int *out) {
  if (s == NULL || out == NULL) {
    return -1;
  }
  if (strcmp(s, "1") == 0 || strcmp(s, "true") == 0 ||
      strcmp(s, "TRUE") == 0 || strcmp(s, "yes") == 0 ||
      strcmp(s, "on") == 0) {
    *out = 1;
    return 0;
  }
  if (strcmp(s, "0") == 0 || strcmp(s, "false") == 0 ||
      strcmp(s, "FALSE") == 0 || strcmp(s, "no") == 0 ||
      strcmp(s, "off") == 0) {
    *out = 0;
    return 0;
  }
  return -1;
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

int cmdb_hex_encode(const unsigned char *in, size_t len,
                    char *out, size_t out_size) {
  static const char hex[] = "0123456789abcdef";
  size_t i;

  if (out == NULL || out_size < (len * 2) + 1 || (in == NULL && len > 0)) {
    return -1;
  }
  for (i = 0; i < len; i++) {
    out[i * 2] = hex[(in[i] >> 4) & 0x0F];
    out[i * 2 + 1] = hex[in[i] & 0x0F];
  }
  out[len * 2] = '\0';
  return 0;
}

int cmdb_hex_decode(const char *in, unsigned char *out, size_t out_len) {
  size_t i;

  if (in == NULL || out == NULL) {
    return -1;
  }
  if (strlen(in) != out_len * 2) {
    return -1;
  }
  for (i = 0; i < out_len; i++) {
    int high = hex_value(in[i * 2]);
    int low = hex_value(in[i * 2 + 1]);
    if (high < 0 || low < 0) {
      return -1;
    }
    out[i] = (unsigned char)((high << 4) | low);
  }
  return 0;
}

void cmdb_strbuf_init(cmdb_strbuf_t *sb) {
  if (sb == NULL) {
    return;
  }
  sb->cap = 256;
  sb->len = 0;
  sb->data = cmdb_malloc(sb->cap);
  sb->data[0] = '\0';
}

void cmdb_strbuf_free(cmdb_strbuf_t *sb) {
  if (sb == NULL) {
    return;
  }
  free(sb->data);
  sb->data = NULL;
  sb->len = 0;
  sb->cap = 0;
}

static void strbuf_reserve(cmdb_strbuf_t *sb, size_t extra) {
  size_t needed;
  size_t cap;

  if (sb == NULL) {
    return;
  }
  if (sb->data == NULL) {
    cmdb_strbuf_init(sb);
  }
  if (extra > SIZE_MAX - sb->len - 1) {
    (void)fprintf(stderr, "cmdb-lite: string buffer overflow\n");
    abort();
  }
  needed = sb->len + extra + 1;
  if (needed <= sb->cap) {
    return;
  }
  cap = sb->cap;
  while (cap < needed) {
    if (cap > SIZE_MAX / 2) {
      cap = needed;
      break;
    }
    cap *= 2;
  }
  sb->data = cmdb_realloc(sb->data, cap);
  sb->cap = cap;
}

void cmdb_strbuf_append_n(cmdb_strbuf_t *sb, const char *s, size_t n) {
  if (sb == NULL || s == NULL || n == 0) {
    return;
  }
  strbuf_reserve(sb, n);
  memcpy(sb->data + sb->len, s, n);
  sb->len += n;
  sb->data[sb->len] = '\0';
}

void cmdb_strbuf_append(cmdb_strbuf_t *sb, const char *s) {
  if (s != NULL) {
    cmdb_strbuf_append_n(sb, s, strlen(s));
  }
}

void cmdb_strbuf_appendf(cmdb_strbuf_t *sb, const char *fmt, ...) {
  va_list args;
  va_list copy;
  int needed;

  if (sb == NULL || fmt == NULL) {
    return;
  }

  va_start(args, fmt);
  va_copy(copy, args);
  needed = vsnprintf(NULL, 0, fmt, copy);
  va_end(copy);
  if (needed < 0) {
    va_end(args);
    return;
  }

  strbuf_reserve(sb, (size_t)needed);
  (void)vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, args);
  va_end(args);
  sb->len += (size_t)needed;
}

void cmdb_strbuf_append_xml_escaped(cmdb_strbuf_t *sb, const char *s) {
  const unsigned char *p;

  if (sb == NULL) {
    return;
  }
  if (s == NULL) {
    return;
  }

  p = (const unsigned char *)s;
  while (*p != '\0') {
    switch (*p) {
      case '&': cmdb_strbuf_append(sb, "&amp;"); break;
      case '<': cmdb_strbuf_append(sb, "&lt;"); break;
      case '>': cmdb_strbuf_append(sb, "&gt;"); break;
      case '"': cmdb_strbuf_append(sb, "&quot;"); break;
      case '\'': cmdb_strbuf_append(sb, "&apos;"); break;
      case '\n': cmdb_strbuf_append(sb, "&#10;"); break;
      case '\r': cmdb_strbuf_append(sb, "&#13;"); break;
      case '\t': cmdb_strbuf_append(sb, "&#9;"); break;
      default:
        if (*p < 0x20 || *p == 0x7F) {
          cmdb_strbuf_append(sb, " ");
        } else {
          char one[2];
          one[0] = (char)*p;
          one[1] = '\0';
          cmdb_strbuf_append(sb, one);
        }
        break;
    }
    p++;
  }
}

char *cmdb_strbuf_steal(cmdb_strbuf_t *sb) {
  char *data;
  if (sb == NULL) {
    return NULL;
  }
  if (sb->data == NULL) {
    cmdb_strbuf_init(sb);
  }
  data = sb->data;
  sb->data = NULL;
  sb->len = 0;
  sb->cap = 0;
  return data;
}
