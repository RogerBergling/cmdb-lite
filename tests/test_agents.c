#include "test.h"
#include "test_util.h"

#include "cmdb/agents.h"
#include "cmdb/ci.h"
#include "cmdb/schema.h"
#include "cmdb/types.h"
#include "cmdb/util.h"

#include <stdlib.h>
#include <string.h>

static void agent_test_facts(cmdb_agent_facts_t *facts, const char *hostname,
                             const char *machine) {
  memset(facts, 0, sizeof(*facts));
  (void)cmdb_str_copy(facts->display_name, sizeof(facts->display_name),
                      hostname);
  (void)cmdb_str_copy(facts->hostname, sizeof(facts->hostname), hostname);
  (void)cmdb_str_copy(facts->machine_id, sizeof(facts->machine_id), machine);
  (void)cmdb_str_copy(facts->os_family, sizeof(facts->os_family), "linux");
  (void)cmdb_str_copy(facts->os_name, sizeof(facts->os_name), "Ubuntu");
  (void)cmdb_str_copy(facts->os_version, sizeof(facts->os_version), "24.04");
  (void)cmdb_str_copy(facts->architecture, sizeof(facts->architecture),
                      "x86_64");
  (void)cmdb_str_copy(facts->agent_version, sizeof(facts->agent_version),
                      "0.7.0");
}

static void agent_test_app(cmdb_agent_app_t *app, const char *key,
                           const char *name, const char *version) {
  memset(app, 0, sizeof(*app));
  (void)cmdb_str_copy(app->app_key, sizeof(app->app_key), key);
  (void)cmdb_str_copy(app->name, sizeof(app->name), name);
  (void)cmdb_str_copy(app->version, sizeof(app->version), version);
  (void)cmdb_str_copy(app->publisher, sizeof(app->publisher), "Example");
  (void)cmdb_str_copy(app->source, sizeof(app->source), "deb");
  (void)cmdb_str_copy(app->architecture, sizeof(app->architecture), "amd64");
  app->size_kb = 42;
}

void test_agents(void) {
  char *dir = test_temp_dir();
  char path[4096];
  char schema[32];
  cmdb_db_t *db;
  cmdb_error_t err;
  char enrollment_token[CMDB_AGENT_TOKEN_LEN + 1];
  char agent_token[CMDB_AGENT_TOKEN_LEN + 1];
  cmdb_agent_token_t token;
  cmdb_agent_token_t *tokens = NULL;
  size_t token_count = 0;
  cmdb_agent_facts_t facts;
  cmdb_agent_t agent;
  cmdb_agent_t *agents = NULL;
  size_t agent_count = 0;
  cmdb_agent_app_t apps[3];
  cmdb_agent_app_t *current = NULL;
  size_t current_count = 0;
  cmdb_agent_report_t report;
  cmdb_agent_report_t *reports = NULL;
  cmdb_agent_event_t *events = NULL;
  size_t report_count = 0;
  size_t event_count = 0;
  cmdb_ci_type_t type;
  cmdb_ci_input_t input;
  char ci_id[37];
  int reused = 0;

  TEST_ASSERT(dir != NULL);
  if (dir == NULL) {
    return;
  }
  cmdb_error_clear(&err);
  db = test_create_db(dir, "agents.cmdb", path, sizeof(path));
  TEST_ASSERT(db != NULL);
  if (db == NULL) {
    free(dir);
    return;
  }
  TEST_ASSERT(cmdb_schema_meta_get(db, "schema_version", schema,
                                   sizeof(schema), &err) == 0);
  TEST_ASSERT(strcmp(schema, "5") == 0);

  TEST_ASSERT(cmdb_agent_token_create(db, "test enrollment", "tester", 3600,
                                      enrollment_token, &token, &err) == 0);
  TEST_ASSERT(strlen(enrollment_token) == CMDB_AGENT_TOKEN_LEN);
  TEST_ASSERT(strlen(token.token_prefix) == CMDB_AGENT_TOKEN_PREFIX_LEN);
  TEST_ASSERT(cmdb_agent_token_list(db, &tokens, &token_count, &err) == 0);
  TEST_ASSERT(token_count == 1);
  cmdb_agent_tokens_free(tokens);
  tokens = NULL;

  agent_test_facts(&facts, "app01", "machine-app01");
  TEST_ASSERT(cmdb_agent_enroll(db, enrollment_token, &facts, agent_token,
                                &agent, &reused, &err) == 0);
  TEST_ASSERT(reused == 0);
  TEST_ASSERT(strcmp(agent.hostname, "app01") == 0);
  TEST_ASSERT(strlen(agent_token) == CMDB_AGENT_TOKEN_LEN);
  TEST_ASSERT(cmdb_agent_auth(db, agent_token, &agent, &err) == 0);
  TEST_ASSERT(strcmp(agent.machine_id, "machine-app01") == 0);

  TEST_ASSERT(cmdb_types_find_by_name(db, "Linux Server", &type, &err) == 0);
  memset(&input, 0, sizeof(input));
  input.name = "app01";
  input.type_id = type.id;
  TEST_ASSERT(cmdb_ci_create(db, &input, ci_id, &err) == 0);
  TEST_ASSERT_MSG(cmdb_agent_get(db, agent.id, &agent, &err) == 0, err.detail);
  TEST_ASSERT_MSG(cmdb_agent_link_ci(db, agent.id, ci_id, &err) == 0,
                  err.message);
  TEST_ASSERT_MSG(cmdb_agent_get(db, agent.id, &agent, &err) == 0,
                  err.message);
  TEST_ASSERT(strcmp(agent.ci_id, ci_id) == 0);
  TEST_ASSERT(strcmp(agent.ci_name, "app01") == 0);

  agent_test_app(&apps[0], "deb:bash:amd64", "bash", "5.2");
  agent_test_app(&apps[1], "deb:curl:amd64", "curl", "8.5");
  {
    char now[21];
    cmdb_iso8601_now(now);
    TEST_ASSERT(cmdb_agent_report(db, agent.id, &facts, now, apps, 2, "", 1,
                                  CMDB_AGENT_RETENTION_DAYS,
                                  CMDB_AGENT_RETENTION_REPORTS, &report,
                                  &err) == 0);
  }
  TEST_ASSERT(report.app_count == 2);
  TEST_ASSERT(report.added_count == 2);
  TEST_ASSERT(report.removed_count == 0);
  TEST_ASSERT(report.updated_count == 0);
  TEST_ASSERT(report.complete == 1);

  agent_test_app(&apps[0], "deb:bash:amd64", "bash", "5.3");
  agent_test_app(&apps[1], "deb:vim:amd64", "vim", "9.1");
  {
    char now[21];
    cmdb_iso8601_now(now);
    TEST_ASSERT_MSG(cmdb_agent_report(db, agent.id, &facts, now, apps, 2, "", 1,
                                  CMDB_AGENT_RETENTION_DAYS, 2, &report,
                                  &err) == 0, err.detail);
  }
  TEST_ASSERT_MSG(report.added_count == 1, "added");
  TEST_ASSERT_MSG(report.removed_count == 1, "removed");
  TEST_ASSERT_MSG(report.updated_count == 1, "updated");

  TEST_ASSERT_MSG(cmdb_agent_apps(db, agent.id, &current, &current_count, &err) ==
              0, err.message);
  TEST_ASSERT(current_count == 2);
  if (current_count == 2) {
    TEST_ASSERT(strcmp(current[0].name, "bash") == 0);
    TEST_ASSERT(strcmp(current[0].version, "5.3") == 0);
    TEST_ASSERT(strcmp(current[1].name, "vim") == 0);
  }
  cmdb_agent_apps_free(current);
  current = NULL;

  TEST_ASSERT(cmdb_agent_history(db, agent.id, 10, 50, &reports, &report_count,
                                 &events, &event_count, &err) == 0);
  TEST_ASSERT(report_count == 2);
  TEST_ASSERT(event_count == 5);
  cmdb_agent_history_free(reports, events);
  reports = NULL;
  events = NULL;

  TEST_ASSERT(cmdb_agent_list(db, &agents, &agent_count, &err) == 0);
  TEST_ASSERT(agent_count == 1);
  TEST_ASSERT(agents[0].app_count == 2);
  cmdb_agent_list_free(agents);
  agents = NULL;

  {
    char now[21];
    cmdb_iso8601_now(now);
    TEST_ASSERT(cmdb_agent_report(db, agent.id, &facts, now, apps, 2, "", 1,
                                  CMDB_AGENT_RETENTION_DAYS, 2, &report,
                                  &err) == 0);
  }
  TEST_ASSERT(cmdb_agent_history(db, agent.id, 10, 50, &reports, &report_count,
                                 &events, &event_count, &err) == 0);
  TEST_ASSERT(report_count == 2);
  cmdb_agent_history_free(reports, events);
  reports = NULL;
  events = NULL;

  /* A partial collector report must not remove applications that were simply
   * not readable during this run. */
  {
    char now[21];
    cmdb_agent_app_t partial[1];
    agent_test_app(&partial[0], "deb:bash:amd64", "bash", "5.4");
    cmdb_iso8601_now(now);
    TEST_ASSERT(cmdb_agent_report(db, agent.id, &facts, now, partial, 1,
                                  "partial registry read", 0,
                                  CMDB_AGENT_RETENTION_DAYS, 2, &report,
                                  &err) == 0);
    TEST_ASSERT(report.complete == 0);
    TEST_ASSERT(report.removed_count == 0);
    TEST_ASSERT(cmdb_agent_apps(db, agent.id, &current, &current_count,
                                &err) == 0);
    TEST_ASSERT(current_count == 2);
    cmdb_agent_apps_free(current);
    current = NULL;
  }

  TEST_ASSERT(cmdb_agent_revoke(db, agent.id, &err) == 0);
  TEST_ASSERT(cmdb_agent_get(db, agent.id, &agent, &err) == 0);
  TEST_ASSERT(strcmp(agent.status, "revoked") == 0);
  TEST_ASSERT(cmdb_agent_auth(db, agent_token, &agent, &err) != 0);

  TEST_ASSERT(cmdb_agent_token_create(db, "second", "tester", 3600,
                                      enrollment_token, &token, &err) == 0);
  TEST_ASSERT(cmdb_agent_token_revoke(db, token.id, &err) == 0);
  agent_test_facts(&facts, "app01", "machine-app01");
  TEST_ASSERT(cmdb_agent_enroll(db, enrollment_token, &facts, agent_token,
                                &agent, &reused, &err) != 0);

  TEST_ASSERT(cmdb_agent_token_create(db, "reuse", "tester", 3600,
                                      enrollment_token, &token, &err) == 0);
  TEST_ASSERT(cmdb_agent_enroll(db, enrollment_token, &facts, agent_token,
                                &agent, &reused, &err) == 0);
  TEST_ASSERT(reused == 1);
  TEST_ASSERT(strcmp(agent.status, "active") == 0);
  TEST_ASSERT(strcmp(agent.ci_id, ci_id) == 0);
  TEST_ASSERT(cmdb_agent_enroll(db, enrollment_token, &facts, agent_token,
                                &agent, &reused, &err) != 0);

  /* Simulate a v4 database and verify the v5 agent-table migration. */
  TEST_ASSERT_MSG(cmdb_db_exec(
                db,
                "DROP TABLE agent_app_events;"
                "DROP TABLE agent_apps;"
                "DROP TABLE agent_reports;"
                "DROP TABLE agent_enrollment_tokens;"
                "DROP TABLE agents;"
                "DROP INDEX IF EXISTS idx_ci_attributes_lookup;",
                &err) == 0, err.detail);
  TEST_ASSERT(cmdb_schema_meta_set(db, "schema_version", "4", &err) == 0);
  cmdb_db_close(db);
  db = test_open_db(path);
  TEST_ASSERT(db != NULL);
  if (db != NULL) {
    TEST_ASSERT(cmdb_schema_meta_get(db, "schema_version", schema,
                                     sizeof(schema), &err) == 0);
    TEST_ASSERT(strcmp(schema, "5") == 0);
    TEST_ASSERT(cmdb_agent_list(db, &agents, &agent_count, &err) == 0);
    TEST_ASSERT(agent_count == 0);
    cmdb_agent_list_free(agents);
    agents = NULL;
  }

  cmdb_db_close(db);
  test_cleanup_db(dir, "agents.cmdb");
  free(dir);
}
