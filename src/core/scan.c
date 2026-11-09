#include "cmdb/scan.h"

#include "cmdb/util.h"

#include <expat.h>

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
#endif

#define SCAN_MAX_XML_DEPTH 64

static int scan_parse_ipv4(const char *text, size_t len,
                           unsigned int octets[4]) {
  size_t index = 0;
  size_t i;
  int part = 0;

  if (text == NULL || len == 0) {
    return -1;
  }
  while (part < 4) {
    size_t start = index;
    unsigned int value = 0;

    if (index >= len) {
      return -1;
    }
    while (index < len && text[index] != '.') {
      if (text[index] < '0' || text[index] > '9') {
        return -1;
      }
      if (index - start >= 3) {
        return -1;
      }
      value = value * 10u + (unsigned int)(text[index] - '0');
      index++;
    }
    if (index == start || value > 255u) {
      return -1;
    }
    if (index - start > 1 && text[start] == '0') {
      return -1;
    }
    octets[part++] = value;
    if (part < 4) {
      if (index >= len || text[index] != '.') {
        return -1;
      }
      index++;
    }
  }
  for (i = 0; i < len; i++) {
    if (text[i] == '.' || (text[i] >= '0' && text[i] <= '9')) {
      continue;
    }
    return -1;
  }
  return index == len ? 0 : -1;
}

int cmdb_scan_validate_target(const char *target, char normalized[64],
                              cmdb_error_t *err) {
  char work[128];
  char *slash;
  size_t len;
  size_t start = 0;
  size_t end;
  unsigned int octets[4];
  int prefix = 32;
  char result[64];

  if (target == NULL || normalized == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "A scan target is required");
    return -1;
  }
  len = strlen(target);
  if (len == 0 || len >= sizeof(work)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Scan target must be an IPv4 address or CIDR");
    return -1;
  }
  while (start < len && (target[start] == ' ' || target[start] == '\t')) {
    start++;
  }
  end = len;
  while (end > start &&
         (target[end - 1] == ' ' || target[end - 1] == '\t')) {
    end--;
  }
  if (end == start || end - start >= sizeof(work)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Scan target must be an IPv4 address or CIDR");
    return -1;
  }
  (void)memcpy(work, target + start, end - start);
  work[end - start] = '\0';

  slash = strchr(work, '/');
  if (slash != NULL) {
    char *prefix_text;
    size_t prefix_len;
    int value = 0;
    size_t i;

    if (strchr(slash + 1, '/') != NULL) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "Scan target must contain at most one /prefix");
      return -1;
    }
    *slash = '\0';
    prefix_text = slash + 1;
    prefix_len = strlen(prefix_text);
    if (prefix_len == 0 || prefix_len > 2) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "CIDR prefix must be between 24 and 32");
      return -1;
    }
    for (i = 0; i < prefix_len; i++) {
      if (prefix_text[i] < '0' || prefix_text[i] > '9') {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "CIDR prefix must be numeric");
        return -1;
      }
      value = value * 10 + (prefix_text[i] - '0');
    }
    if (prefix_len > 1 && prefix_text[0] == '0') {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "CIDR prefix must be between 24 and 32");
      return -1;
    }
    if (value < 24 || value > 32) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "Scanning is limited to /24 or smaller networks");
      return -1;
    }
    prefix = value;
  }

  if (scan_parse_ipv4(work, strlen(work), octets) != 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Scan target must be a valid IPv4 address or CIDR");
    return -1;
  }
  if (slash == NULL) {
    (void)snprintf(result, sizeof(result), "%u.%u.%u.%u/32",
                   octets[0], octets[1], octets[2], octets[3]);
  } else {
    (void)snprintf(result, sizeof(result), "%u.%u.%u.%u/%d",
                   octets[0], octets[1], octets[2], octets[3], prefix);
  }
  (void)cmdb_str_copy(normalized, 64, result);
  return 0;
}

static int scan_parse_port_number(const char *text, size_t len,
                                  unsigned int *value) {
  unsigned int result = 0;
  size_t i;

  if (text == NULL || len == 0 || len > 5) {
    return -1;
  }
  for (i = 0; i < len; i++) {
    if (text[i] < '0' || text[i] > '9') {
      return -1;
    }
    result = result * 10u + (unsigned int)(text[i] - '0');
  }
  if (result < 1u || result > 65535u) {
    return -1;
  }
  *value = result;
  return 0;
}

int cmdb_scan_validate_ports(const char *ports, char normalized[1024],
                             cmdb_error_t *err) {
  unsigned char seen[65536 / 8];
  char work[1024];
  size_t len;
  size_t start = 0;
  size_t end;
  size_t index;
  size_t distinct = 0;

  if (ports == NULL || normalized == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "A port specification is required");
    return -1;
  }
  len = strlen(ports);
  if (len == 0 || len >= sizeof(work)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Port specification is empty or too long");
    return -1;
  }
  while (start < len && (ports[start] == ' ' || ports[start] == '\t')) {
    start++;
  }
  end = len;
  while (end > start &&
         (ports[end - 1] == ' ' || ports[end - 1] == '\t')) {
    end--;
  }
  if (end == start || end - start >= sizeof(work)) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Port specification is empty or too long");
    return -1;
  }
  (void)memcpy(work, ports + start, end - start);
  work[end - start] = '\0';
  memset(seen, 0, sizeof(seen));

  index = 0;
  while (index < strlen(work)) {
    size_t token_start = index;
    size_t dash;
    unsigned int first;
    unsigned int last;
    unsigned int port;

    while (index < strlen(work) && work[index] != ',') {
      index++;
    }
    if (index == token_start) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "Port specification contains an empty entry");
      return -1;
    }
    dash = (size_t)-1;
    {
      size_t i;
      for (i = token_start; i < index; i++) {
        char c = work[i];
        if (c == '-') {
          if (dash != (size_t)-1) {
            cmdb_error_set(err, CMDB_ERR_VALIDATION,
                           "Port ranges must use a single dash");
            return -1;
          }
          dash = i;
        } else if (c < '0' || c > '9') {
          cmdb_error_set(err, CMDB_ERR_VALIDATION,
                         "Ports must be numeric (for example 22,80,443)");
          return -1;
        }
      }
    }
    if (dash == (size_t)-1) {
      if (scan_parse_port_number(work + token_start, index - token_start,
                                 &first) != 0) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Port numbers must be between 1 and 65535");
        return -1;
      }
      last = first;
    } else {
      if (dash == token_start || dash + 1 == index ||
          scan_parse_port_number(work + token_start, dash - token_start,
                                 &first) != 0 ||
          scan_parse_port_number(work + dash + 1, index - dash - 1,
                                 &last) != 0 ||
          first > last) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Port ranges must be ascending and between 1 and 65535");
        return -1;
      }
    }
    for (port = first; port <= last; port++) {
      unsigned int byte = port / 8u;
      unsigned char mask = (unsigned char)(1u << (port % 8u));
      if ((seen[byte] & mask) == 0) {
        seen[byte] = (unsigned char)(seen[byte] | mask);
        distinct++;
        if (distinct > CMDB_SCAN_MAX_PORTS) {
          cmdb_error_set(err, CMDB_ERR_LIMIT,
                         "At most 256 distinct ports can be scanned");
          return -1;
        }
      }
      if (port == last) {
        break;
      }
    }
    if (index < strlen(work)) {
      index++;
      if (index == strlen(work)) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Port specification contains an empty entry");
        return -1;
      }
    }
  }
  if (distinct == 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Port specification must contain at least one port");
    return -1;
  }
  (void)cmdb_str_copy(normalized, 1024, work);
  return 0;
}

static void scan_normalize_mac(const char *input, char output[32]) {
  size_t out = 0;
  size_t i;

  if (input == NULL || output == NULL) {
    return;
  }
  for (i = 0; input[i] != '\0' && out + 1 < 32; i++) {
    char c = input[i];
    if (c == '-') {
      c = ':';
    } else if (c >= 'A' && c <= 'F') {
      c = (char)(c - 'A' + 'a');
    }
    output[out++] = c;
  }
  output[out] = '\0';
}

typedef struct {
  cmdb_scan_result_t *result;
  cmdb_scan_host_t current;
  size_t capacity;
  int in_host;
  int in_port;
  int depth;
  int failed;
  char current_port[16];
  char current_protocol[16];
  char error[256];
} scan_parser_t;

static void scan_parser_fail(scan_parser_t *parser, const char *message) {
  if (!parser->failed) {
    parser->failed = 1;
    (void)cmdb_str_copy(parser->error, sizeof(parser->error),
                        message != NULL ? message : "Invalid scan XML");
  }
}

static const char *scan_xml_attr(const XML_Char **atts, const char *name) {
  size_t i;

  if (atts == NULL) {
    return NULL;
  }
  for (i = 0; atts[i] != NULL && atts[i + 1] != NULL; i += 2) {
    if (strcmp(atts[i], name) == 0) {
      return atts[i + 1];
    }
  }
  return NULL;
}

static void scan_append_port(scan_parser_t *parser, const char *port,
                             const char *protocol) {
  char token[32];
  size_t token_len;
  size_t used;

  if (port == NULL || protocol == NULL) {
    return;
  }
  (void)snprintf(token, sizeof(token), "%s/%s", port, protocol);
  token_len = strlen(token);
  used = strlen(parser->current.open_ports);
  if (parser->current.port_count >= CMDB_SCAN_MAX_HOST_PORTS ||
      used + token_len + 2 >= sizeof(parser->current.open_ports)) {
    parser->current.ports_truncated = 1;
    return;
  }
  if (used > 0) {
    parser->current.open_ports[used++] = ',';
  }
  (void)memcpy(parser->current.open_ports + used, token, token_len + 1);
  parser->current.port_count++;
}

static void XMLCALL scan_start_element(void *userData,
                                       const XML_Char *name,
                                       const XML_Char **atts) {
  scan_parser_t *parser = (scan_parser_t *)userData;

  if (parser->failed) {
    return;
  }
  parser->depth++;
  if (parser->depth > SCAN_MAX_XML_DEPTH) {
    scan_parser_fail(parser, "Scan XML is nested too deeply");
    return;
  }
  if (parser->depth == 1 && strcmp(name, "nmaprun") != 0) {
    scan_parser_fail(parser, "Scan XML is not nmap output");
    return;
  }
  if (strcmp(name, "host") == 0) {
    if (parser->in_host) {
      scan_parser_fail(parser, "Scan XML contains nested host elements");
      return;
    }
    memset(&parser->current, 0, sizeof(parser->current));
    parser->in_host = 1;
  } else if (strcmp(name, "status") == 0 && parser->in_host) {
    const char *state = scan_xml_attr(atts, "state");
    if (state != NULL) {
      (void)cmdb_str_copy(parser->current.status,
                          sizeof(parser->current.status), state);
    }
  } else if (strcmp(name, "address") == 0 && parser->in_host) {
    const char *type = scan_xml_attr(atts, "addrtype");
    const char *address = scan_xml_attr(atts, "addr");
    if (type != NULL && address != NULL) {
      if ((strcmp(type, "ipv4") == 0 || strcmp(type, "ipv6") == 0) &&
          parser->current.ip_address[0] == '\0') {
        (void)cmdb_str_copy(parser->current.ip_address,
                            sizeof(parser->current.ip_address), address);
      } else if (strcmp(type, "mac") == 0) {
        const char *vendor = scan_xml_attr(atts, "vendor");
        scan_normalize_mac(address, parser->current.mac_address);
        if (vendor != NULL) {
          (void)cmdb_str_copy(parser->current.vendor,
                              sizeof(parser->current.vendor), vendor);
        }
      }
    }
  } else if (strcmp(name, "hostname") == 0 && parser->in_host) {
    const char *hostname = scan_xml_attr(atts, "name");
    if (hostname != NULL && parser->current.hostname[0] == '\0') {
      (void)cmdb_str_copy(parser->current.hostname,
                          sizeof(parser->current.hostname), hostname);
    }
  } else if (strcmp(name, "port") == 0 && parser->in_host) {
    const char *port = scan_xml_attr(atts, "portid");
    const char *protocol = scan_xml_attr(atts, "protocol");
    if (port == NULL || protocol == NULL) {
      scan_parser_fail(parser, "Scan XML contains an invalid port element");
      return;
    }
    (void)cmdb_str_copy(parser->current_port,
                        sizeof(parser->current_port), port);
    (void)cmdb_str_copy(parser->current_protocol,
                        sizeof(parser->current_protocol), protocol);
    parser->in_port = 1;
  } else if (strcmp(name, "state") == 0 && parser->in_port) {
    const char *state = scan_xml_attr(atts, "state");
    if (state != NULL && strcmp(state, "open") == 0) {
      scan_append_port(parser, parser->current_port,
                       parser->current_protocol);
    }
  } else if (strcmp(name, "osmatch") == 0 && parser->in_host) {
    const char *os_name = scan_xml_attr(atts, "name");
    if (os_name != NULL && parser->current.os_guess[0] == '\0') {
      (void)cmdb_str_copy(parser->current.os_guess,
                          sizeof(parser->current.os_guess), os_name);
    }
  } else if (strcmp(name, "osclass") == 0 && parser->in_host) {
    const char *family = scan_xml_attr(atts, "osfamily");
    if (family != NULL && parser->current.os_guess[0] == '\0') {
      (void)cmdb_str_copy(parser->current.os_guess,
                          sizeof(parser->current.os_guess), family);
    }
  }
}

static void scan_finish_host(scan_parser_t *parser) {
  if (!parser->in_host) {
    return;
  }
  if (parser->current.ip_address[0] != '\0' &&
      (parser->current.port_count > 0 ||
       strcmp(parser->current.status, "up") == 0)) {
    if (parser->result->host_count >= CMDB_SCAN_MAX_HOSTS) {
      parser->result->truncated = 1;
    } else {
      if (parser->result->host_count >= parser->capacity) {
        size_t next = parser->capacity == 0 ? 16 : parser->capacity * 2;
        if (next > CMDB_SCAN_MAX_HOSTS) {
          next = CMDB_SCAN_MAX_HOSTS;
        }
        parser->result->hosts = cmdb_realloc(
          parser->result->hosts, next * sizeof(*parser->result->hosts));
        parser->capacity = next;
      }
      parser->result->hosts[parser->result->host_count++] = parser->current;
    }
  }
  memset(&parser->current, 0, sizeof(parser->current));
  parser->in_host = 0;
  parser->in_port = 0;
}

static void XMLCALL scan_end_element(void *userData, const XML_Char *name) {
  scan_parser_t *parser = (scan_parser_t *)userData;

  if (parser->failed) {
    return;
  }
  if (strcmp(name, "port") == 0) {
    parser->in_port = 0;
    parser->current_port[0] = '\0';
    parser->current_protocol[0] = '\0';
  } else if (strcmp(name, "host") == 0) {
    scan_finish_host(parser);
  }
  if (parser->depth > 0) {
    parser->depth--;
  }
}

static void XMLCALL scan_doctype(void *userData, const XML_Char *doctypeName,
                                 const XML_Char *systemId,
                                 const XML_Char *publicId,
                                 int hasInternalSubset) {
  scan_parser_t *parser = (scan_parser_t *)userData;
  (void)doctypeName;
  (void)systemId;
  (void)publicId;
  (void)hasInternalSubset;
  scan_parser_fail(parser, "Scan XML must not contain a document type");
}

static void XMLCALL scan_entity_decl(
  void *userData, const XML_Char *entityName, int isParameterEntity,
  const XML_Char *value, int valueLength, const XML_Char *base,
  const XML_Char *systemId, const XML_Char *publicId,
  const XML_Char *notationName) {
  scan_parser_t *parser = (scan_parser_t *)userData;
  (void)entityName;
  (void)isParameterEntity;
  (void)value;
  (void)valueLength;
  (void)base;
  (void)systemId;
  (void)publicId;
  (void)notationName;
  scan_parser_fail(parser, "Scan XML must not declare entities");
}

int cmdb_scan_parse_xml(const char *xml, size_t len,
                        cmdb_scan_result_t *result, cmdb_error_t *err) {
  XML_Parser parser;
  scan_parser_t state;
  enum XML_Status status;

  if (xml == NULL || result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid scan XML arguments");
    return -1;
  }
  memset(result, 0, sizeof(*result));
  memset(&state, 0, sizeof(state));
  if (len == 0) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION, "The scan produced no output");
    return -1;
  }
  if (len > CMDB_SCAN_MAX_XML_BYTES) {
    cmdb_error_set(err, CMDB_ERR_LIMIT, "The scan output is too large");
    return -1;
  }
  parser = XML_ParserCreate(NULL);
  if (parser == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot create the XML parser");
    return -1;
  }
  state.result = result;
  XML_SetUserData(parser, &state);
  XML_SetElementHandler(parser, scan_start_element, scan_end_element);
  XML_SetStartDoctypeDeclHandler(parser, scan_doctype);
  XML_SetEntityDeclHandler(parser, scan_entity_decl);
  (void)XML_SetParamEntityParsing(parser, XML_PARAM_ENTITY_PARSING_NEVER);
  status = XML_Parse(parser, xml, (int)len, 1);
  if (status != XML_STATUS_OK || state.failed) {
#if XML_MAJOR_VERSION > 2 || \
  (XML_MAJOR_VERSION == 2 && XML_MINOR_VERSION >= 9)
    uint64_t line = XML_GetCurrentLineNumber64(parser);
#else
    XML_Size line = XML_GetCurrentLineNumber(parser);
#endif
    enum XML_Error code = XML_GetErrorCode(parser);
    if (state.failed) {
      cmdb_error_set(err, CMDB_ERR_VALIDATION, "%s", state.error);
    } else {
      cmdb_error_set(err, CMDB_ERR_VALIDATION,
                     "Cannot parse nmap XML output");
      cmdb_error_set_detail(err, "line %llu: %s",
                            (unsigned long long)line,
                            XML_ErrorString(code));
    }
    XML_ParserFree(parser);
    cmdb_scan_result_free(result);
    return -1;
  }
  XML_ParserFree(parser);
  (void)cmdb_str_copy(result->method, sizeof(result->method), "nmap");
  return 0;
}

void cmdb_scan_result_free(cmdb_scan_result_t *result) {
  if (result == NULL) {
    return;
  }
  free(result->hosts);
  memset(result, 0, sizeof(*result));
}

#ifndef _WIN32

static int scan_is_executable(const char *path) {
  struct stat st;
  if (path == NULL || path[0] == '\0') {
    return 0;
  }
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
    return 0;
  }
  return access(path, X_OK) == 0;
}

static char *scan_find_nmap(void) {
  const char *override = getenv("CMDB_NMAP");
  const char *path;
  const char *cursor;

  if (override != NULL && override[0] != '\0') {
    return scan_is_executable(override) ? cmdb_strdup(override) : NULL;
  }
  path = getenv("PATH");
  if (path == NULL || path[0] == '\0') {
    path = "/usr/local/bin:/usr/bin:/bin";
  }
  cursor = path;
  while (*cursor != '\0') {
    const char *separator = strchr(cursor, ':');
    size_t dir_len = separator != NULL ? (size_t)(separator - cursor)
                                       : strlen(cursor);
    if (dir_len > 0) {
      char candidate[4096];
      int written = snprintf(candidate, sizeof(candidate), "%.*s/nmap",
                             (int)dir_len, cursor);
      if (written > 0 && (size_t)written < sizeof(candidate) &&
          scan_is_executable(candidate)) {
        return cmdb_strdup(candidate);
      }
    }
    if (separator == NULL) {
      break;
    }
    cursor = separator + 1;
  }
  return NULL;
}

static int scan_temp_file(char path[4096], cmdb_error_t *err) {
  const char *dir = getenv("TMPDIR");
  char template_path[4096];
  int written;
  int fd;

  if (dir == NULL || dir[0] == '\0') {
    dir = "/tmp";
  }
  written = snprintf(template_path, sizeof(template_path),
                     "%s/cmdb-scan-XXXXXX", dir);
  if (written < 0 || (size_t)written >= sizeof(template_path)) {
    cmdb_error_set(err, CMDB_ERR_IO, "Temporary directory path is too long");
    return -1;
  }
  fd = mkstemp(template_path);
  if (fd < 0) {
    cmdb_error_set(err, CMDB_ERR_IO,
                   "Cannot create a private scan output file");
    cmdb_error_set_detail(err, "%s", strerror(errno));
    return -1;
  }
  (void)close(fd);
  (void)cmdb_str_copy(path, 4096, template_path);
  return 0;
}

static int scan_run_process(const char *executable, char *const argv[],
                            int timeout_seconds, cmdb_error_t *err) {
  pid_t pid;
  int status = 0;
  long waited_ms = 0;
  struct timespec interval;
  int rc;

  rc = posix_spawn(&pid, executable, NULL, NULL, argv, environ);
  if (rc != 0) {
    cmdb_error_set(err, CMDB_ERR_IO, "Cannot start nmap");
    cmdb_error_set_detail(err, "%s", strerror(rc));
    return -1;
  }
  interval.tv_sec = 0;
  interval.tv_nsec = 100000000L;
  for (;;) {
    pid_t child = waitpid(pid, &status, WNOHANG);
    if (child == pid) {
      break;
    }
    if (child < 0 && errno != EINTR) {
      cmdb_error_set(err, CMDB_ERR_IO, "Cannot wait for nmap");
      cmdb_error_set_detail(err, "%s", strerror(errno));
      return -1;
    }
    if (waited_ms >= (long)timeout_seconds * 1000L) {
      (void)kill(pid, SIGKILL);
      (void)waitpid(pid, &status, 0);
      cmdb_error_set(err, CMDB_ERR_LIMIT,
                     "The nmap scan timed out");
      cmdb_error_set_detail(err, "timeout: %d seconds", timeout_seconds);
      return -1;
    }
    (void)nanosleep(&interval, NULL);
    waited_ms += 100;
  }
  if (WIFEXITED(status)) {
    int code = WEXITSTATUS(status);
    if (code != 0) {
      cmdb_error_set(err, CMDB_ERR_IO, "nmap failed");
      cmdb_error_set_detail(err, "exit code %d", code);
      return -1;
    }
    return 0;
  }
  if (WIFSIGNALED(status)) {
    cmdb_error_set(err, CMDB_ERR_IO, "nmap was terminated");
    cmdb_error_set_detail(err, "signal %d", WTERMSIG(status));
    return -1;
  }
  cmdb_error_set(err, CMDB_ERR_IO, "nmap did not exit normally");
  return -1;
}

#else

static char *scan_find_nmap(void) {
  const char *override = getenv("CMDB_NMAP");
  char buffer[32768];
  DWORD length;
  DWORD attributes;

  if (override != NULL && override[0] != '\0') {
    attributes = GetFileAttributesA(override);
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      return cmdb_strdup(override);
    }
    return NULL;
  }
  length = SearchPathA(NULL, "nmap", ".exe", sizeof(buffer), buffer, NULL);
  if (length == 0 || length >= sizeof(buffer)) {
    return NULL;
  }
  attributes = GetFileAttributesA(buffer);
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return NULL;
  }
  return cmdb_strdup(buffer);
}

static int scan_temp_file(char path[4096], cmdb_error_t *err) {
  char directory[32768];
  char file[32768];
  DWORD length;

  length = GetTempPathA((DWORD)sizeof(directory), directory);
  if (length == 0 || length >= sizeof(directory)) {
    cmdb_error_set(err, CMDB_ERR_IO,
                   "Cannot determine the temporary directory");
    return -1;
  }
  if (GetTempFileNameA(directory, "cmd", 0, file) == 0) {
    cmdb_error_set(err, CMDB_ERR_IO,
                   "Cannot create a private scan output file");
    return -1;
  }
  (void)cmdb_str_copy(path, 4096, file);
  return 0;
}

static void scan_append_windows_arg(cmdb_strbuf_t *buffer, const char *arg) {
  const char *cursor;
  size_t backslashes = 0;
  int quote = arg[0] == '\0' || strpbrk(arg, " \t\"") != NULL;

  if (buffer->len > 0) {
    cmdb_strbuf_append(buffer, " ");
  }
  if (!quote) {
    cmdb_strbuf_append(buffer, arg);
    return;
  }
  cmdb_strbuf_append(buffer, "\"");
  for (cursor = arg; *cursor != '\0'; cursor++) {
    if (*cursor == '\\') {
      backslashes++;
    } else if (*cursor == '"') {
      size_t i;
      for (i = 0; i < backslashes * 2 + 1; i++) {
        cmdb_strbuf_append(buffer, "\\");
      }
      cmdb_strbuf_append(buffer, "\"");
      backslashes = 0;
    } else {
      size_t i;
      for (i = 0; i < backslashes; i++) {
        cmdb_strbuf_append(buffer, "\\");
      }
      backslashes = 0;
      cmdb_strbuf_append_n(buffer, cursor, 1);
    }
  }
  {
    size_t i;
    for (i = 0; i < backslashes * 2; i++) {
      cmdb_strbuf_append(buffer, "\\");
    }
  }
  cmdb_strbuf_append(buffer, "\"");
}

static int scan_run_process(const char *executable, char *const argv[],
                            int timeout_seconds, cmdb_error_t *err) {
  cmdb_strbuf_t command;
  STARTUPINFOA startup;
  PROCESS_INFORMATION process;
  DWORD wait_result;
  DWORD exit_code = 0;
  size_t i;
  BOOL created;

  cmdb_strbuf_init(&command);
  cmdb_strbuf_append(&command, executable);
  for (i = 1; argv[i] != NULL; i++) {
    scan_append_windows_arg(&command, argv[i]);
  }
  memset(&startup, 0, sizeof(startup));
  startup.cb = sizeof(startup);
  memset(&process, 0, sizeof(process));
  created = CreateProcessA(NULL, command.data, NULL, NULL, FALSE,
                           CREATE_NO_WINDOW, NULL, NULL, &startup, &process);
  cmdb_strbuf_free(&command);
  if (!created) {
    cmdb_error_set(err, CMDB_ERR_IO, "Cannot start nmap");
    cmdb_error_set_detail(err, "Windows error %lu",
                          (unsigned long)GetLastError());
    return -1;
  }
  (void)CloseHandle(process.hThread);
  wait_result = WaitForSingleObject(process.hProcess,
                                    (DWORD)timeout_seconds * 1000u);
  if (wait_result == WAIT_TIMEOUT) {
    (void)TerminateProcess(process.hProcess, 1);
    (void)WaitForSingleObject(process.hProcess, 5000);
    (void)CloseHandle(process.hProcess);
    cmdb_error_set(err, CMDB_ERR_LIMIT, "The nmap scan timed out");
    cmdb_error_set_detail(err, "timeout: %d seconds", timeout_seconds);
    return -1;
  }
  if (wait_result != WAIT_OBJECT_0 ||
      !GetExitCodeProcess(process.hProcess, &exit_code)) {
    (void)CloseHandle(process.hProcess);
    cmdb_error_set(err, CMDB_ERR_IO, "Cannot wait for nmap");
    return -1;
  }
  (void)CloseHandle(process.hProcess);
  if (exit_code != 0) {
    cmdb_error_set(err, CMDB_ERR_IO, "nmap failed");
    cmdb_error_set_detail(err, "exit code %lu", (unsigned long)exit_code);
    return -1;
  }
  return 0;
}

#endif

int cmdb_scan_nmap_run(const char *target, const char *ports,
                       int timeout_seconds, cmdb_scan_result_t *result,
                       cmdb_error_t *err) {
  char normalized_target[64];
  char normalized_ports[1024];
  char *nmap;
  char xml_path[4096];
  char *xml = NULL;
  size_t xml_len = 0;
  int rc = -1;
  char *argv[20];
  char arg_connect[] = "-sT";
  char arg_no_ping[] = "-Pn";
  char arg_no_dns[] = "-n";
  char arg_open[] = "--open";
  char arg_rate[] = "--max-rate";
  char arg_rate_value[] = "500";
  char arg_retries[] = "--max-retries";
  char arg_retries_value[] = "1";
  char arg_host_timeout[] = "--host-timeout";
  char arg_host_timeout_value[] = "30s";
  char arg_ports[] = "-p";
  char arg_output[] = "-oX";

  if (result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid scan result pointer");
    return -1;
  }
  memset(result, 0, sizeof(*result));
  if (cmdb_scan_validate_target(target, normalized_target, err) != 0 ||
      cmdb_scan_validate_ports(ports, normalized_ports, err) != 0) {
    return -1;
  }
  if (timeout_seconds < CMDB_SCAN_MIN_TIMEOUT ||
      timeout_seconds > CMDB_SCAN_MAX_TIMEOUT) {
    cmdb_error_set(err, CMDB_ERR_VALIDATION,
                   "Scan timeout must be between 10 and 600 seconds");
    return -1;
  }
  nmap = scan_find_nmap();
  if (nmap == NULL) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND,
                   "nmap is not installed or is not on PATH");
    cmdb_error_set_detail(err,
                          "Install nmap or set CMDB_NMAP to its full path");
    return -1;
  }
  if (scan_temp_file(xml_path, err) != 0) {
    free(nmap);
    return -1;
  }
  argv[0] = nmap;
  argv[1] = arg_connect;
  argv[2] = arg_no_ping;
  argv[3] = arg_no_dns;
  argv[4] = arg_open;
  argv[5] = arg_rate;
  argv[6] = arg_rate_value;
  argv[7] = arg_retries;
  argv[8] = arg_retries_value;
  argv[9] = arg_host_timeout;
  argv[10] = arg_host_timeout_value;
  argv[11] = arg_ports;
  argv[12] = normalized_ports;
  argv[13] = arg_output;
  argv[14] = xml_path;
  argv[15] = normalized_target;
  argv[16] = NULL;

  (void)cmdb_str_copy(result->target, sizeof(result->target),
                      normalized_target);
  (void)cmdb_str_copy(result->method, sizeof(result->method), "nmap");
  (void)snprintf(result->command, sizeof(result->command),
                 "nmap -sT -Pn -n --open --max-rate 500 --max-retries 1 "
                 "--host-timeout 30s -p %s -oX %s %s",
                 normalized_ports, xml_path, normalized_target);
  cmdb_iso8601_now(result->started_at);
  if (scan_run_process(nmap, argv, timeout_seconds, err) != 0) {
    goto cleanup;
  }
  cmdb_iso8601_now(result->finished_at);
  if (cmdb_file_read_all(xml_path, CMDB_SCAN_MAX_XML_BYTES,
                         &xml, &xml_len, err) != 0) {
    goto cleanup;
  }
  {
    cmdb_scan_result_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    if (cmdb_scan_parse_xml(xml, xml_len, &parsed, err) != 0) {
      goto cleanup;
    }
    result->hosts = parsed.hosts;
    result->host_count = parsed.host_count;
    result->truncated = parsed.truncated;
    parsed.hosts = NULL;
    cmdb_scan_result_free(&parsed);
  }
  cmdb_iso8601_now(result->finished_at);
  rc = 0;

cleanup:
  free(xml);
  free(nmap);
  (void)cmdb_file_remove(xml_path);
  if (rc != 0) {
    cmdb_scan_result_free(result);
  }
  return rc;
}
