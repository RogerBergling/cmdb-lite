#include "cmdb/util.h"

#include <sodium.h>

#include <stdio.h>
#include <string.h>

int cmdb_random_uuid(char out[37]) {
  unsigned char bytes[16];

  if (out == NULL) {
    return -1;
  }
  if (sodium_init() < 0) {
    return -1;
  }

  randombytes_buf(bytes, sizeof(bytes));
  bytes[6] = (unsigned char)((bytes[6] & 0x0F) | 0x40);
  bytes[8] = (unsigned char)((bytes[8] & 0x3F) | 0x80);

  (void)snprintf(out, 37,
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                 "%02x%02x%02x%02x%02x%02x",
                 bytes[0], bytes[1], bytes[2], bytes[3],
                 bytes[4], bytes[5], bytes[6], bytes[7],
                 bytes[8], bytes[9], bytes[10], bytes[11],
                 bytes[12], bytes[13], bytes[14], bytes[15]);
  return 0;
}

int cmdb_uuid_valid(const char *s) {
  size_t i;

  if (s == NULL || strlen(s) != 36) {
    return 0;
  }
  for (i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != '-') {
        return 0;
      }
    } else {
      char c = s[i];
      if (!((c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F'))) {
        return 0;
      }
    }
  }
  return 1;
}
