#ifndef CMDB_SCAN_H
#define CMDB_SCAN_H

#include "cmdb/db.h"
#include "cmdb/error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CMDB_SCAN_MAX_TARGET 64
#define CMDB_SCAN_MAX_PORT_SPEC 1024
#define CMDB_SCAN_MAX_HOSTS 256u
#define CMDB_SCAN_MAX_PORTS 256u
#define CMDB_SCAN_MAX_HOST_PORTS 256u
#define CMDB_SCAN_OPEN_PORTS_SIZE 3072
#define CMDB_SCAN_MAX_XML_BYTES (8u * 1024u * 1024u)
#define CMDB_SCAN_DEFAULT_PORTS "22,80,443,445,3389,8080"
#define CMDB_SCAN_DEFAULT_TIMEOUT 180
#define CMDB_SCAN_MIN_TIMEOUT 10
#define CMDB_SCAN_MAX_TIMEOUT 600

typedef struct {
  char ci_id[37];
  char ci_name[256];
  char type_name[128];
  char reasons[128];
  int score;
} cmdb_scan_match_t;

typedef struct {
  char ip_address[64];
  char hostname[256];
  char mac_address[32];
  char vendor[128];
  char os_guess[128];
  char services[2048];
  char status[16];
  char open_ports[CMDB_SCAN_OPEN_PORTS_SIZE];
  size_t port_count;
  int ports_truncated;
  char asset_id[37];
  char review_status[16];
  cmdb_scan_match_t *matches;
  size_t match_count;
} cmdb_scan_host_t;

typedef struct {
  char target[CMDB_SCAN_MAX_TARGET];
  char method[16];
  char command[8192];
  char started_at[21];
  char finished_at[21];
  char ports[CMDB_SCAN_MAX_PORT_SPEC];
  char job_id[37];
  char status[16];
  char error[512];
  int service_detection;
  int os_detection;
  cmdb_scan_host_t *hosts;
  size_t host_count;
  int truncated;
} cmdb_scan_result_t;

typedef struct {
  char id[37];
  char target[CMDB_SCAN_MAX_TARGET];
  char status[16];
  char started_at[21];
  char finished_at[21];
  size_t host_count;
  size_t pending_count;
  int truncated;
  int service_detection;
  int os_detection;
} cmdb_scan_job_summary_t;

/*
 * Validate and normalise an IPv4 address or CIDR. Only prefixes from /24 to
 * /32 are accepted, so a job covers at most 256 addresses.
 */
int cmdb_scan_validate_target(const char *target, char normalized[64],
                              cmdb_error_t *err);

/*
 * Validate a numeric nmap port specification such as "22,80,443" or
 * "8000-8100". At most 256 distinct ports are allowed.
 */
int cmdb_scan_validate_ports(const char *ports, char normalized[1024],
                             cmdb_error_t *err);

/*
 * Parse nmap XML into a heap-owned result. The parser rejects DTDs and
 * entity declarations and bounds element depth and host/port counts.
 */
int cmdb_scan_parse_xml(const char *xml, size_t len,
                        cmdb_scan_result_t *result, cmdb_error_t *err);

void cmdb_scan_result_free(cmdb_scan_result_t *result);

/*
 * Run an unprivileged TCP connect scan with nmap. Uses a fixed argv vector
 * (never a shell), a private temporary XML file, a hard timeout, and a
 * validated IPv4/CIDR target. nmap is resolved from CMDB_NMAP or PATH.
 */
int cmdb_scan_nmap_run(const char *target, const char *ports,
                       int timeout_seconds, cmdb_scan_result_t *result,
                       cmdb_error_t *err);

/*
 * Extended runner used by the asynchronous scan manager. Service detection
 * adds -sV; OS detection adds -O and requires root/administrator privileges.
 * cancel_flag is polled while nmap runs.
 */
int cmdb_scan_nmap_run_ex(const char *target, const char *ports,
                          int timeout_seconds, int service_detection,
                          int os_detection, volatile int *cancel_flag,
                          cmdb_scan_result_t *result, cmdb_error_t *err);

/* Persisted scan staging and matching. */
int cmdb_scan_store_begin(cmdb_db_t *db, const char *target, const char *ports,
                          int service_detection, int os_detection,
                          char job_id[37], cmdb_error_t *err);
int cmdb_scan_store_finish(cmdb_db_t *db, cmdb_scan_result_t *result,
                           const char *status, const char *error,
                           cmdb_error_t *err);
int cmdb_scan_store_load(cmdb_db_t *db, const char *job_id,
                         cmdb_scan_result_t *result, cmdb_error_t *err);
int cmdb_scan_store_latest(cmdb_db_t *db, cmdb_scan_result_t *result,
                           cmdb_error_t *err);
int cmdb_scan_jobs_list(cmdb_db_t *db, cmdb_scan_job_summary_t **out,
                        size_t *count, cmdb_error_t *err);
int cmdb_scan_jobs_delete(cmdb_db_t *db, const char *job_id,
                          cmdb_error_t *err);
int cmdb_scan_jobs_recover(cmdb_db_t *db, cmdb_error_t *err);
int cmdb_scan_match_assets(cmdb_db_t *db, cmdb_scan_result_t *result,
                           cmdb_error_t *err);
int cmdb_scan_asset_set_review(cmdb_db_t *db, const char *asset_id,
                               const char *review_status,
                               const char *matched_ci_id,
                               cmdb_error_t *err);
int cmdb_scan_job_pending(cmdb_db_t *db, const char *job_id,
                          size_t *pending, cmdb_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_SCAN_H */
