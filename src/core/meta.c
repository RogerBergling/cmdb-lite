#include "cmdb/meta.h"

#include "cmdb/util.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_line_value(const char *line, const char *prefix,
                            const char **out) {
  size_t len = strlen(prefix);
  if (strncmp(line, prefix, len) != 0) {
    return 0;
  }
  *out = line + len;
  return 1;
}

void cmdb_meta_init_random(cmdb_meta_t *meta) {
  if (meta == NULL) {
    return;
  }
  memset(meta, 0, sizeof(*meta));
  meta->version = CMDB_META_VERSION;
  meta->opslimit = CMDB_KDF_OPSLIMIT;
  meta->memlimit = CMDB_KDF_MEMLIMIT;
  (void)cmdb_crypto_random(meta->salt, sizeof(meta->salt), NULL);
}

int cmdb_meta_validate(const cmdb_meta_t *meta, cmdb_error_t *err) {
  if (meta == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Missing metadata");
    return -1;
  }
  if (meta->version != CMDB_META_VERSION) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Unsupported database metadata version");
    return -1;
  }
  if (meta->opslimit < CMDB_KDF_OPSLIMIT || meta->opslimit > 64u) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Unsafe Argon2id operations parameter");
    return -1;
  }
  if (meta->memlimit < CMDB_KDF_MEMLIMIT ||
      meta->memlimit > (size_t)1024u * 1024u * 1024u) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Unsafe Argon2id memory parameter");
    return -1;
  }
  return 0;
}

int cmdb_meta_create(const char *db_path, const cmdb_meta_t *meta,
                     cmdb_error_t *err) {
  char *meta_path;
  cmdb_strbuf_t sb;
  char salt_hex[33];
  char checksum[65];
  int rc;

  if (db_path == NULL || meta == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid metadata arguments");
    return -1;
  }
  if (cmdb_meta_validate(meta, err) != 0) {
    return -1;
  }

  meta_path = cmdb_path_meta(db_path);
  if (meta_path == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot build metadata path");
    return -1;
  }
  if (cmdb_path_exists(meta_path)) {
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "Metadata file already exists");
    cmdb_error_set_detail(err, "%s", meta_path);
    free(meta_path);
    return -1;
  }

  if (cmdb_hex_encode(meta->salt, sizeof(meta->salt),
                      salt_hex, sizeof(salt_hex)) != 0) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot encode metadata salt");
    free(meta_path);
    return -1;
  }

  cmdb_strbuf_init(&sb);
  cmdb_strbuf_appendf(&sb, "CMDB-META %u\n", meta->version);
  cmdb_strbuf_append(&sb, "kdf=argon2id13\n");
  cmdb_strbuf_appendf(&sb, "opslimit=%llu\n", meta->opslimit);
  cmdb_strbuf_appendf(&sb, "memlimit=%zu\n", meta->memlimit);
  cmdb_strbuf_appendf(&sb, "salt=%s\n", salt_hex);

  if (cmdb_crypto_hash_hex(sb.data, sb.len, checksum, err) != 0) {
    cmdb_strbuf_free(&sb);
    free(meta_path);
    return -1;
  }
  cmdb_strbuf_appendf(&sb, "checksum=%s\n", checksum);

  rc = cmdb_file_write_atomic(meta_path, sb.data, sb.len, 0600, err);
  if (rc == 0) {
    (void)cmdb_file_chmod_0600(meta_path);
  }

  cmdb_strbuf_free(&sb);
  free(meta_path);
  return rc;
}

int cmdb_meta_read(const char *db_path, cmdb_meta_t *meta,
                   cmdb_error_t *err) {
  char *meta_path;
  char *data = NULL;
  size_t len = 0;
  size_t checksum_pos = 0;
  int found_checksum = 0;
  char checksum[65];
  char computed[65];
  char *line;
  char *cursor;
  int seen_kdf = 0;
  int seen_ops = 0;
  int seen_mem = 0;
  int seen_salt = 0;
  int seen_header = 0;
  int rc = -1;

  if (db_path == NULL || meta == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid metadata arguments");
    return -1;
  }

  memset(meta, 0, sizeof(*meta));
  meta_path = cmdb_path_meta(db_path);
  if (meta_path == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot build metadata path");
    return -1;
  }

  if (!cmdb_is_regular_file(meta_path)) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND,
                   "Database metadata file is missing");
    cmdb_error_set_detail(err, "%s", meta_path);
    free(meta_path);
    return -1;
  }

  if (cmdb_file_read_all(meta_path, 4096, &data, &len, err) != 0) {
    free(meta_path);
    return -1;
  }

  for (size_t i = 0; i + 9 <= len; i++) {
    if ((i == 0 || data[i - 1] == '\n') &&
        strncmp(data + i, "checksum=", 9) == 0) {
      checksum_pos = i;
      found_checksum = 1;
    }
  }
  if (!found_checksum || checksum_pos == 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Metadata checksum is missing");
    goto cleanup;
  }

  {
    size_t j = checksum_pos + 9;
    size_t k = 0;
    while (j < len && data[j] != '\n' && data[j] != '\r' &&
           k + 1 < sizeof(checksum)) {
      checksum[k++] = data[j++];
    }
    checksum[k] = '\0';
  }
  if (strlen(checksum) != 64) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Metadata checksum is malformed");
    goto cleanup;
  }

  if (cmdb_crypto_hash_hex(data, checksum_pos, computed, err) != 0) {
    goto cleanup;
  }
  if (strcmp(checksum, computed) != 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Database metadata is corrupt or has been modified");
    goto cleanup;
  }

  data[checksum_pos] = '\0';
  cursor = data;
  while (*cursor != '\0') {
    const char *value = NULL;
    char *newline;

    line = cursor;
    newline = strpbrk(cursor, "\r\n");
    if (newline != NULL) {
      char *next = newline + 1;
      *newline = '\0';
      while (*next == '\r' || *next == '\n') {
        next++;
      }
      cursor = next;
    } else {
      cursor += strlen(cursor);
    }

    if (line[0] == '\0') {
      continue;
    }

    if (!seen_header) {
      if (strcmp(line, "CMDB-META 1") == 0) {
        meta->version = 1;
        seen_header = 1;
      } else {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Unsupported metadata header");
        goto cleanup;
      }
    } else if (parse_line_value(line, "kdf=", &value)) {
      if (strcmp(value, "argon2id13") != 0) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Unsupported key derivation function");
        goto cleanup;
      }
      seen_kdf = 1;
    } else if (parse_line_value(line, "opslimit=", &value)) {
      char *end = NULL;
      unsigned long long parsed;
      if (seen_ops) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Duplicate Argon2id operations parameter");
        goto cleanup;
      }
      errno = 0;
      parsed = strtoull(value, &end, 10);
      if (end == value || *end != '\0' || errno != 0) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Invalid Argon2id operations parameter");
        goto cleanup;
      }
      meta->opslimit = parsed;
      seen_ops = 1;
    } else if (parse_line_value(line, "memlimit=", &value)) {
      char *end = NULL;
      unsigned long long parsed;
      if (seen_mem) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Duplicate Argon2id memory parameter");
        goto cleanup;
      }
      errno = 0;
      parsed = strtoull(value, &end, 10);
      if (end == value || *end != '\0' || errno != 0 ||
          parsed > (unsigned long long)SIZE_MAX) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Invalid Argon2id memory parameter");
        goto cleanup;
      }
      meta->memlimit = (size_t)parsed;
      seen_mem = 1;
    } else if (parse_line_value(line, "salt=", &value)) {
      if (seen_salt || strlen(value) != CMDB_KDF_SALT_LEN * 2 ||
          cmdb_hex_decode(value, meta->salt, sizeof(meta->salt)) != 0) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Invalid metadata salt");
        goto cleanup;
      }
      seen_salt = 1;
    }
  }

  if (!seen_header || !seen_kdf || !seen_ops || !seen_mem || !seen_salt) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Database metadata is incomplete");
    goto cleanup;
  }
  if (cmdb_meta_validate(meta, err) != 0) {
    goto cleanup;
  }

  rc = 0;

cleanup:
  free(data);
  free(meta_path);
  return rc;
}

int cmdb_meta_exists(const char *db_path) {
  char *meta_path;
  int exists;
  if (db_path == NULL) {
    return 0;
  }
  meta_path = cmdb_path_meta(db_path);
  if (meta_path == NULL) {
    return 0;
  }
  exists = cmdb_path_exists(meta_path);
  free(meta_path);
  return exists;
}

int cmdb_meta_delete(const char *db_path) {
  char *meta_path;
  int rc;
  if (db_path == NULL) {
    return -1;
  }
  meta_path = cmdb_path_meta(db_path);
  if (meta_path == NULL) {
    return -1;
  }
  rc = cmdb_file_remove(meta_path);
  free(meta_path);
  return rc;
}
