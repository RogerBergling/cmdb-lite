#ifndef CMDB_AGENTS_H
#define CMDB_AGENTS_H

#include "cmdb/db.h"
#include "cmdb/error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CMDB_AGENT_MAX_APPS 20000u
#define CMDB_AGENT_MAX_FILE_BYTES (32u * 1024u * 1024u)
#define CMDB_AGENT_RETENTION_DAYS 365
#define CMDB_AGENT_RETENTION_REPORTS 90
#define CMDB_AGENT_TOKEN_LEN 64
#define CMDB_AGENT_TOKEN_PREFIX_LEN 8

typedef struct {
  char id[37];
  char display_name[256];
  char hostname[256];
  char machine_id[128];
  char os_family[32];
  char os_name[128];
  char os_version[128];
  char architecture[32];
  char agent_version[64];
  char status[16];
  char enrolled_at[21];
  char last_report_at[21];
  char last_seen_ip[64];
  char ci_id[37];
  char ci_name[256];
  char suggested_ci_id[37];
  char suggested_ci_name[256];
  size_t app_count;
  size_t report_count;
  char created_at[21];
  char updated_at[21];
} cmdb_agent_t;

typedef struct {
  char id[37];
  char token_prefix[CMDB_AGENT_TOKEN_PREFIX_LEN + 1];
  char label[128];
  char created_by[64];
  char created_at[21];
  char expires_at[21];
  char used_at[21];
  char revoked_at[21];
} cmdb_agent_token_t;

typedef struct {
  char app_key[256];
  char name[512];
  char version[256];
  char publisher[256];
  char source[32];
  char architecture[32];
  char install_date[32];
  char install_location[1024];
  long long size_kb;
  char first_seen_at[21];
  char last_seen_at[21];
} cmdb_agent_app_t;

typedef struct {
  char id[37];
  char collected_at[21];
  char received_at[21];
  size_t app_count;
  size_t added_count;
  size_t removed_count;
  size_t updated_count;
  int complete;
  char collector_errors[2048];
} cmdb_agent_report_t;

typedef struct {
  char id[37];
  char report_id[37];
  char app_key[256];
  char event_type[16];
  char name[512];
  char old_version[256];
  char new_version[256];
  char detail[1024];
  char occurred_at[21];
} cmdb_agent_event_t;

typedef struct {
  char display_name[256];
  char hostname[256];
  char machine_id[128];
  char os_family[32];
  char os_name[128];
  char os_version[128];
  char architecture[32];
  char agent_version[64];
  char last_seen_ip[64];
} cmdb_agent_facts_t;

/* Enrollment-token lifecycle. The plaintext token is returned once and never
 * stored; only its SHA-256 hash is persisted. */
int cmdb_agent_token_create(cmdb_db_t *db, const char *label,
                            const char *created_by, int ttl_seconds,
                            char token_out[CMDB_AGENT_TOKEN_LEN + 1],
                            cmdb_agent_token_t *out, cmdb_error_t *err);
int cmdb_agent_token_list(cmdb_db_t *db, cmdb_agent_token_t **out,
                          size_t *count, cmdb_error_t *err);
int cmdb_agent_token_revoke(cmdb_db_t *db, const char *token_id,
                            cmdb_error_t *err);
void cmdb_agent_tokens_free(cmdb_agent_token_t *tokens);

/* Enrollment exchanges a short-lived enrollment token for a per-host agent
 * token. Re-enrolling the same machine_id rotates the token and preserves the
 * existing host record and CI link. */
int cmdb_agent_enroll(cmdb_db_t *db, const char *enrollment_token,
                      const cmdb_agent_facts_t *facts,
                      char agent_token_out[CMDB_AGENT_TOKEN_LEN + 1],
                      cmdb_agent_t *out, int *reused, cmdb_error_t *err);
int cmdb_agent_auth(cmdb_db_t *db, const char *agent_token,
                    cmdb_agent_t *out, cmdb_error_t *err);

/* Ingest one report and return the compact report/change summary. The caller
 * must already have authenticated the agent. */
int cmdb_agent_report(cmdb_db_t *db, const char *agent_id,
                      const cmdb_agent_facts_t *facts,
                      const char *collected_at,
                      const cmdb_agent_app_t *apps, size_t app_count,
                      const char *collector_errors, int complete,
                      int retention_days, int max_reports,
                      cmdb_agent_report_t *out, cmdb_error_t *err);

int cmdb_agent_list(cmdb_db_t *db, cmdb_agent_t **out, size_t *count,
                    cmdb_error_t *err);
int cmdb_agent_get(cmdb_db_t *db, const char *agent_id, cmdb_agent_t *out,
                   cmdb_error_t *err);
int cmdb_agent_apps(cmdb_db_t *db, const char *agent_id,
                    cmdb_agent_app_t **out, size_t *count, cmdb_error_t *err);
int cmdb_agent_history(cmdb_db_t *db, const char *agent_id,
                       size_t report_limit, size_t event_limit,
                       cmdb_agent_report_t **reports, size_t *report_count,
                       cmdb_agent_event_t **events, size_t *event_count,
                       cmdb_error_t *err);
int cmdb_agent_link_ci(cmdb_db_t *db, const char *agent_id,
                       const char *ci_id, cmdb_error_t *err);
int cmdb_agent_revoke(cmdb_db_t *db, const char *agent_id,
                      cmdb_error_t *err);
int cmdb_agent_delete(cmdb_db_t *db, const char *agent_id,
                      cmdb_error_t *err);
int cmdb_agent_prune(cmdb_db_t *db, int retention_days, int max_reports,
                     size_t *deleted, cmdb_error_t *err);

void cmdb_agent_list_free(cmdb_agent_t *agents);
void cmdb_agent_apps_free(cmdb_agent_app_t *apps);
void cmdb_agent_history_free(cmdb_agent_report_t *reports,
                             cmdb_agent_event_t *events);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_AGENTS_H */
