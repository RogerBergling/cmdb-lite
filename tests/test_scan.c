#include "test.h"
#include "test_util.h"

#include "cmdb/scan.h"
#include "cmdb/scan_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

static void expect_bad_target(const char *target) {
  char normalized[64];
  cmdb_error_t err;
  cmdb_error_clear(&err);
  TEST_ASSERT_MSG(cmdb_scan_validate_target(target, normalized, &err) != 0,
                  target != NULL ? target : "(null)");
}

static void expect_good_target(const char *target, const char *expected) {
  char normalized[64];
  cmdb_error_t err;
  cmdb_error_clear(&err);
  TEST_ASSERT_MSG(cmdb_scan_validate_target(target, normalized, &err) == 0,
                  target);
  TEST_ASSERT(strcmp(normalized, expected) == 0);
}

static void expect_bad_ports(const char *ports) {
  char normalized[1024];
  cmdb_error_t err;
  cmdb_error_clear(&err);
  TEST_ASSERT_MSG(cmdb_scan_validate_ports(ports, normalized, &err) != 0,
                  ports != NULL ? ports : "(null)");
}

static void expect_good_ports(const char *ports, const char *expected) {
  char normalized[1024];
  cmdb_error_t err;
  cmdb_error_clear(&err);
  TEST_ASSERT_MSG(cmdb_scan_validate_ports(ports, normalized, &err) == 0,
                  ports);
  TEST_ASSERT(strcmp(normalized, expected) == 0);
}

static void test_scan_targets(void) {
  expect_good_target("10.20.0.0/24", "10.20.0.0/24");
  expect_good_target(" 10.20.0.7 ", "10.20.0.7/32");
  expect_good_target("192.168.1.42/32", "192.168.1.42/32");
  expect_good_target("172.16.4.0/25", "172.16.4.0/25");

  expect_bad_target("");
  expect_bad_target(NULL);
  expect_bad_target("10.20.0.0/23");
  expect_bad_target("10.20.0.0/16");
  expect_bad_target("10.20.0.0/33");
  expect_bad_target("10.20.0.0/");
  expect_bad_target("10.20.0.0/024");
  expect_bad_target("10.20.0.0/2a");
  expect_bad_target("10.20.0.0/24/1");
  expect_bad_target("10.20.0.256/24");
  expect_bad_target("10.20.0/24");
  expect_bad_target("10.20.0.1.2/24");
  expect_bad_target("010.20.0.1/24");
  expect_bad_target("host.example");
  expect_bad_target("-10.20.0.1");
  expect_bad_target("10.20.0.1;rm -rf /");
  expect_bad_target("10.20.0.1 /24");
}

static void test_scan_ports(void) {
  expect_good_ports(CMDB_SCAN_DEFAULT_PORTS, CMDB_SCAN_DEFAULT_PORTS);
  expect_good_ports("22,80,443", "22,80,443");
  expect_good_ports("8000-8010", "8000-8010");
  expect_good_ports("1-256", "1-256");
  expect_good_ports("65535", "65535");
  expect_good_ports("22,22", "22,22");
  expect_good_ports(" 22,80 ", "22,80");

  expect_bad_ports("");
  expect_bad_ports(NULL);
  expect_bad_ports("0");
  expect_bad_ports("65536");
  expect_bad_ports("22,");
  expect_bad_ports(",22");
  expect_bad_ports("22,,80");
  expect_bad_ports("22-");
  expect_bad_ports("-22");
  expect_bad_ports("22-20");
  expect_bad_ports("22;80");
  expect_bad_ports("22 80");
  expect_bad_ports("abc");
  expect_bad_ports("1-257");
  expect_bad_ports("22,80,443,1000-2000");
}

static const char scan_fixture[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
  "<nmaprun scanner=\"nmap\" args=\"nmap -sT -Pn -n --open -p 22,443 "
  "10.20.0.0/24\">\n"
  "  <host>\n"
  "    <status state=\"up\" reason=\"user-set\"/>\n"
  "    <address addr=\"10.20.0.5\" addrtype=\"ipv4\"/>\n"
  "    <address addr=\"AA-BB-CC-DD-EE-01\" addrtype=\"mac\" "
  "vendor=\"Acme Networks\"/>\n"
  "    <hostnames><hostname name=\"app01.example.test\" type=\"PTR\"/>"
  "</hostnames>\n"
  "    <ports>\n"
  "      <port protocol=\"tcp\" portid=\"22\">"
  "<state state=\"open\" reason=\"syn-ack\"/>"
  "<service name=\"ssh\" product=\"OpenSSH\"/></port>\n"
  "      <port protocol=\"tcp\" portid=\"80\">"
  "<state state=\"closed\" reason=\"reset\"/></port>\n"
  "      <port protocol=\"tcp\" portid=\"443\">"
  "<state state=\"open\" reason=\"syn-ack\"/>"
  "<service name=\"https\"/></port>\n"
  "    </ports>\n"
  "    <os><osmatch name=\"Linux 5.15\" accuracy=\"98\"/></os>\n"
  "  </host>\n"
  "  <host>\n"
  "    <status state=\"down\" reason=\"no-response\"/>\n"
  "    <address addr=\"10.20.0.6\" addrtype=\"ipv4\"/>\n"
  "  </host>\n"
  "</nmaprun>\n";

static void test_scan_xml(void) {
  cmdb_scan_result_t result;
  cmdb_error_t err;

  cmdb_error_clear(&err);
  TEST_ASSERT(cmdb_scan_parse_xml(scan_fixture, sizeof(scan_fixture) - 1,
                                  &result, &err) == 0);
  TEST_ASSERT(result.host_count == 1);
  TEST_ASSERT(result.truncated == 0);
  TEST_ASSERT(strcmp(result.method, "nmap") == 0);
  if (result.host_count == 1) {
    const cmdb_scan_host_t *host = &result.hosts[0];
    TEST_ASSERT(strcmp(host->ip_address, "10.20.0.5") == 0);
    TEST_ASSERT(strcmp(host->hostname, "app01.example.test") == 0);
    TEST_ASSERT(strcmp(host->mac_address, "aa:bb:cc:dd:ee:01") == 0);
    TEST_ASSERT(strcmp(host->vendor, "Acme Networks") == 0);
    TEST_ASSERT(strcmp(host->os_guess, "Linux 5.15") == 0);
    TEST_ASSERT(strcmp(host->status, "up") == 0);
    TEST_ASSERT(strcmp(host->open_ports, "22/tcp,443/tcp") == 0);
    TEST_ASSERT(host->port_count == 2);
    TEST_ASSERT(host->ports_truncated == 0);
  }
  cmdb_scan_result_free(&result);
  TEST_ASSERT(result.hosts == NULL);
  TEST_ASSERT(result.host_count == 0);
  cmdb_scan_result_free(&result);

  cmdb_error_clear(&err);
  TEST_ASSERT(cmdb_scan_parse_xml("", 0, &result, &err) != 0);

  cmdb_error_clear(&err);
  {
    const char malformed[] = "<nmaprun><host>";
    TEST_ASSERT(cmdb_scan_parse_xml(malformed, sizeof(malformed) - 1,
                                    &result, &err) != 0);
  }

  cmdb_error_clear(&err);
  {
    const char doctype[] =
      "<?xml version=\"1.0\"?><!DOCTYPE nmaprun>"
      "<nmaprun><host><status state=\"up\"/>"
      "<address addr=\"10.0.0.8\" addrtype=\"ipv4\"/></host></nmaprun>";
    TEST_ASSERT(cmdb_scan_parse_xml(doctype, sizeof(doctype) - 1,
                                    &result, &err) == 0);
    TEST_ASSERT(result.host_count == 1);
    cmdb_scan_result_free(&result);
  }

  cmdb_error_clear(&err);
  {
    const char dtd[] =
      "<!DOCTYPE nmaprun [<!ENTITY x \"y\">]><nmaprun>&x;</nmaprun>";
    TEST_ASSERT(cmdb_scan_parse_xml(dtd, sizeof(dtd) - 1, &result, &err) != 0);
  }

  cmdb_error_clear(&err);
  {
    const char nested[] =
      "<nmaprun><host><host></host></host></nmaprun>";
    TEST_ASSERT(cmdb_scan_parse_xml(nested, sizeof(nested) - 1,
                                    &result, &err) != 0);
  }

  cmdb_error_clear(&err);
  {
    const char wrong_root[] = "<html><body>not nmap</body></html>";
    TEST_ASSERT(cmdb_scan_parse_xml(wrong_root, sizeof(wrong_root) - 1,
                                    &result, &err) != 0);
  }

  cmdb_error_clear(&err);
  {
    const char down_only[] =
      "<nmaprun><host><status state=\"down\"/>"
      "<address addr=\"10.0.0.9\" addrtype=\"ipv4\"/></host></nmaprun>";
    TEST_ASSERT(cmdb_scan_parse_xml(down_only, sizeof(down_only) - 1,
                                    &result, &err) == 0);
    TEST_ASSERT(result.host_count == 0);
    cmdb_scan_result_free(&result);
  }
}

#ifndef _WIN32
static void test_scan_nmap_runner(void) {
  static const char script_text[] =
    "#!/bin/sh\n"
    "out=\"\"\n"
    "while [ \"$#\" -gt 0 ]; do\n"
    "  if [ \"$1\" = \"-oX\" ]; then out=\"$2\"; shift; fi\n"
    "  shift\n"
    "done\n"
    "[ -n \"$out\" ] || exit 2\n"
    "cat > \"$out\" <<'XML'\n"
    "<nmaprun scanner=\"nmap\">\n"
    "<host><status state=\"up\"/>"
    "<address addr=\"10.20.0.55\" addrtype=\"ipv4\"/>"
    "<ports><port protocol=\"tcp\" portid=\"22\">"
    "<state state=\"open\"/></port></ports></host>\n"
    "</nmaprun>\n"
    "XML\n";
  char *dir = test_temp_dir();
  char script[4096];
  FILE *file;
  const char *previous;
  char *saved = NULL;
  cmdb_error_t err;
  cmdb_scan_result_t result;

  TEST_ASSERT(dir != NULL);
  if (dir == NULL) {
    return;
  }
  test_path(script, sizeof(script), dir, "fake-nmap");
  file = fopen(script, "wb");
  TEST_ASSERT(file != NULL);
  if (file == NULL) {
    free(dir);
    return;
  }
  TEST_ASSERT(fwrite(script_text, 1, sizeof(script_text) - 1, file) ==
              sizeof(script_text) - 1);
  (void)fclose(file);
  TEST_ASSERT(chmod(script, 0700) == 0);

  previous = getenv("CMDB_NMAP");
  if (previous != NULL) {
    saved = strdup(previous);
    TEST_ASSERT(saved != NULL);
  }
  TEST_ASSERT(setenv("CMDB_NMAP", script, 1) == 0);
  cmdb_error_clear(&err);
  TEST_ASSERT(cmdb_scan_nmap_run("10.20.0.0/24", "22,443", 30,
                                 &result, &err) == 0);
  TEST_ASSERT(result.host_count == 1);
  if (result.host_count == 1) {
    TEST_ASSERT(strcmp(result.hosts[0].ip_address, "10.20.0.55") == 0);
    TEST_ASSERT(strcmp(result.hosts[0].open_ports, "22/tcp") == 0);
  }
  TEST_ASSERT(result.target[0] != '\0');
  TEST_ASSERT(result.started_at[0] != '\0');
  TEST_ASSERT(result.finished_at[0] != '\0');
  TEST_ASSERT(strstr(result.command, "-sT") != NULL);
  cmdb_scan_result_free(&result);

  if (saved != NULL) {
    TEST_ASSERT(setenv("CMDB_NMAP", saved, 1) == 0);
    free(saved);
  } else {
    TEST_ASSERT(unsetenv("CMDB_NMAP") == 0);
  }
  (void)remove(script);
  (void)rmdir(dir);
  free(dir);
}

static void test_scan_manager(void) {
  static const char slow_script[] =
    "#!/bin/sh\n"
    "sleep 10\n";
  char *dir = test_temp_dir();
  char script[4096];
  FILE *file;
  cmdb_scan_manager_t *manager;
  cmdb_error_t err;
  char job_id[37];
  char status[16];
  cmdb_scan_result_t result;
  int has_result = 0;
  int attempt;
  const char *previous;
  char *saved = NULL;
  struct timespec pause;

  TEST_ASSERT(dir != NULL);
  if (dir == NULL) {
    return;
  }
  previous = getenv("CMDB_NMAP");
  if (previous != NULL) {
    saved = strdup(previous);
  }
  manager = cmdb_scan_manager_new();
  TEST_ASSERT(manager != NULL);
  if (manager == NULL) {
    free(saved);
    free(dir);
    return;
  }

  test_path(script, sizeof(script), dir, "missing-nmap");
  cmdb_error_clear(&err);
  TEST_ASSERT(setenv("CMDB_NMAP", script, 1) == 0);
  TEST_ASSERT(cmdb_random_uuid(job_id) == 0);
  TEST_ASSERT(cmdb_scan_manager_start(manager, job_id, "10.20.0.0/24",
                                      "22,443", 10, 0, 0, &err) == 0);
  pause.tv_sec = 0;
  pause.tv_nsec = 50000000L;
  for (attempt = 0; attempt < 60; attempt++) {
    (void)nanosleep(&pause, NULL);
    (void)cmdb_scan_manager_status(manager, job_id, status, &result,
                                   &has_result, &err);
    cmdb_scan_result_free(&result);
    if (strcmp(status, "failed") == 0 || strcmp(status, "cancelled") == 0) {
      break;
    }
  }
  TEST_ASSERT(strcmp(status, "failed") == 0);
  cmdb_scan_manager_free(manager);

  test_path(script, sizeof(script), dir, "slow-nmap");
  file = fopen(script, "wb");
  TEST_ASSERT(file != NULL);
  if (file != NULL) {
    TEST_ASSERT(fwrite(slow_script, 1, sizeof(slow_script) - 1, file) ==
                sizeof(slow_script) - 1);
    (void)fclose(file);
    TEST_ASSERT(chmod(script, 0700) == 0);
  }
  TEST_ASSERT(setenv("CMDB_NMAP", script, 1) == 0);
  manager = cmdb_scan_manager_new();
  TEST_ASSERT(manager != NULL);
  if (manager != NULL) {
    TEST_ASSERT(cmdb_random_uuid(job_id) == 0);
    TEST_ASSERT(cmdb_scan_manager_start(manager, job_id, "10.20.0.0/24",
                                        "22", 10, 0, 0, &err) == 0);
    (void)nanosleep(&pause, NULL);
    TEST_ASSERT(cmdb_scan_manager_cancel(manager, job_id, &err) == 0);
    for (attempt = 0; attempt < 100; attempt++) {
      (void)nanosleep(&pause, NULL);
      (void)cmdb_scan_manager_status(manager, job_id, status, &result,
                                     &has_result, &err);
      cmdb_scan_result_free(&result);
      if (strcmp(status, "cancelled") == 0) {
        break;
      }
    }
    TEST_ASSERT(strcmp(status, "cancelled") == 0);
    cmdb_scan_manager_free(manager);
  }

  if (saved != NULL) {
    TEST_ASSERT(setenv("CMDB_NMAP", saved, 1) == 0);
    free(saved);
  } else {
    TEST_ASSERT(unsetenv("CMDB_NMAP") == 0);
  }
  (void)remove(script);
  (void)rmdir(dir);
  free(dir);
}

typedef struct {
  int fd;
  volatile int stop;
} live_server_t;

static void *live_http_server(void *argument) {
  live_server_t *server = (live_server_t *)argument;

  while (!server->stop) {
    int client = accept(server->fd, NULL, NULL);
    if (client < 0) {
      struct timespec pause;
      pause.tv_sec = 0;
      pause.tv_nsec = 10000000L;
      (void)nanosleep(&pause, NULL);
      continue;
    }
    {
      static const char response[] =
        "HTTP/1.0 200 OK\r\n"
        "Server: cmdb-test/1.0\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";
      (void)send(client, response, sizeof(response) - 1, 0);
      (void)close(client);
    }
  }
  return NULL;
}

static void test_scan_os_privilege(void) {
  cmdb_scan_result_t result;
  cmdb_error_t err;

  if (geteuid() == 0) {
    return;
  }
  cmdb_error_clear(&err);
  TEST_ASSERT(cmdb_scan_nmap_run_ex("127.0.0.1/32", "22", 10, 0, 1,
                                    NULL, &result, &err) != 0);
  TEST_ASSERT(err.code == CMDB_ERR_VALIDATION);
  TEST_ASSERT(strstr(err.message, "OS detection") != NULL);
}

static void test_scan_live_nmap(void) {
  const char *enabled = getenv("CMDB_TEST_LIVE_NMAP");
  int listener;
  struct sockaddr_in address;
  socklen_t length = sizeof(address);
  char ports[16];
  cmdb_scan_result_t result;
  cmdb_error_t err;
  size_t i;
  live_server_t server;
  pthread_t thread;
  int thread_started = 0;

  if (enabled == NULL || strcmp(enabled, "1") != 0) {
    return;
  }
  listener = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT(listener >= 0);
  if (listener < 0) {
    return;
  }
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  TEST_ASSERT(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
  TEST_ASSERT(listen(listener, 16) == 0);
  TEST_ASSERT(getsockname(listener, (struct sockaddr *)&address, &length) == 0);
  {
    int flags = fcntl(listener, F_GETFL, 0);
    if (flags >= 0) {
      (void)fcntl(listener, F_SETFL, flags | O_NONBLOCK);
    }
  }
  (void)snprintf(ports, sizeof(ports), "%u",
                 (unsigned int)ntohs(address.sin_port));
  server.fd = listener;
  server.stop = 0;
  if (pthread_create(&thread, NULL, live_http_server, &server) == 0) {
    thread_started = 1;
  }
  cmdb_error_clear(&err);
  TEST_ASSERT(cmdb_scan_nmap_run_ex("127.0.0.1/32", ports, 90, 1, 0,
                                    NULL, &result, &err) == 0);
  TEST_ASSERT(result.host_count >= 1);
  for (i = 0; i < result.host_count; i++) {
    if (strcmp(result.hosts[i].ip_address, "127.0.0.1") == 0) {
      char expected[32];
      (void)snprintf(expected, sizeof(expected), "%s/tcp", ports);
      TEST_ASSERT(strstr(result.hosts[i].open_ports, expected) != NULL);
    }
  }
  cmdb_scan_result_free(&result);
  server.stop = 1;
  if (thread_started) {
    (void)pthread_join(thread, NULL);
  }
  (void)close(listener);
}
#endif

void test_scan(void) {
  test_scan_targets();
  test_scan_ports();
  test_scan_xml();
#ifndef _WIN32
  test_scan_nmap_runner();
  test_scan_manager();
  test_scan_os_privilege();
  test_scan_live_nmap();
#endif
}
