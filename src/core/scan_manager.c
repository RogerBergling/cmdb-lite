#include "cmdb/scan_manager.h"

#include "cmdb/util.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

struct cmdb_scan_manager {
#ifdef _WIN32
  CRITICAL_SECTION mutex;
  HANDLE thread;
#else
  pthread_mutex_t mutex;
  pthread_t thread;
#endif
  int thread_valid;
  int active;
  int delivered;
  volatile int cancel;
  char job_id[37];
  char target[64];
  char ports[1024];
  int timeout_seconds;
  int service_detection;
  int os_detection;
  char status[16];
  char error[512];
  cmdb_scan_result_t result;
};

static void scan_manager_lock(cmdb_scan_manager_t *manager) {
#ifdef _WIN32
  EnterCriticalSection(&manager->mutex);
#else
  (void)pthread_mutex_lock(&manager->mutex);
#endif
}

static void scan_manager_unlock(cmdb_scan_manager_t *manager) {
#ifdef _WIN32
  LeaveCriticalSection(&manager->mutex);
#else
  (void)pthread_mutex_unlock(&manager->mutex);
#endif
}

static void scan_manager_join(cmdb_scan_manager_t *manager) {
  if (!manager->thread_valid) {
    return;
  }
#ifdef _WIN32
  (void)WaitForSingleObject(manager->thread, INFINITE);
  (void)CloseHandle(manager->thread);
  manager->thread = NULL;
#else
  (void)pthread_join(manager->thread, NULL);
#endif
  manager->thread_valid = 0;
}

static void scan_manager_worker(cmdb_scan_manager_t *manager) {
  cmdb_scan_result_t local;
  cmdb_error_t err;
  int rc;

  memset(&local, 0, sizeof(local));
  cmdb_error_clear(&err);
  rc = cmdb_scan_nmap_run_ex(manager->target, manager->ports,
                             manager->timeout_seconds,
                             manager->service_detection,
                             manager->os_detection, &manager->cancel,
                             &local, &err);
  scan_manager_lock(manager);
  if (rc == 0) {
    (void)cmdb_str_copy(local.job_id, sizeof(local.job_id), manager->job_id);
    (void)cmdb_str_copy(local.status, sizeof(local.status), "completed");
    manager->result = local;
    memset(&local, 0, sizeof(local));
    (void)cmdb_str_copy(manager->status, sizeof(manager->status),
                        "completed");
  } else {
    (void)cmdb_str_copy(manager->status, sizeof(manager->status),
                        err.code == CMDB_ERR_CANCELLED ? "cancelled"
                                                       : "failed");
    (void)cmdb_str_copy(manager->error, sizeof(manager->error),
                        err.message[0] != '\0' ? err.message : "Scan failed");
    cmdb_scan_result_free(&local);
  }
  manager->active = 0;
  scan_manager_unlock(manager);
}

#ifdef _WIN32
static DWORD WINAPI scan_manager_thread_entry(LPVOID argument) {
  scan_manager_worker((cmdb_scan_manager_t *)argument);
  return 0;
}
#else
static void *scan_manager_thread_entry(void *argument) {
  scan_manager_worker((cmdb_scan_manager_t *)argument);
  return NULL;
}
#endif

cmdb_scan_manager_t *cmdb_scan_manager_new(void) {
  cmdb_scan_manager_t *manager = cmdb_calloc(1, sizeof(*manager));
#ifdef _WIN32
  InitializeCriticalSection(&manager->mutex);
#else
  (void)pthread_mutex_init(&manager->mutex, NULL);
#endif
  return manager;
}

void cmdb_scan_manager_free(cmdb_scan_manager_t *manager) {
  if (manager == NULL) {
    return;
  }
  scan_manager_lock(manager);
  if (manager->active) {
    manager->cancel = 1;
  }
  scan_manager_unlock(manager);
  scan_manager_join(manager);
#ifdef _WIN32
  DeleteCriticalSection(&manager->mutex);
#else
  (void)pthread_mutex_destroy(&manager->mutex);
#endif
  cmdb_scan_result_free(&manager->result);
  cmdb_crypto_wipe(manager->target, sizeof(manager->target));
  free(manager);
}

int cmdb_scan_manager_start(cmdb_scan_manager_t *manager,
                            const char *job_id, const char *target,
                            const char *ports, int timeout_seconds,
                            int service_detection, int os_detection,
                            cmdb_error_t *err) {
  if (manager == NULL || job_id == NULL || target == NULL || ports == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT,
                   "Invalid scan manager arguments");
    return -1;
  }
  scan_manager_lock(manager);
  if (manager->active) {
    scan_manager_unlock(manager);
    cmdb_error_set(err, CMDB_ERR_CONFLICT,
                   "A scan is already running");
    return -1;
  }
  scan_manager_join(manager);
  cmdb_scan_result_free(&manager->result);
  memset(&manager->result, 0, sizeof(manager->result));
  manager->cancel = 0;
  manager->delivered = 0;
  manager->active = 1;
  manager->timeout_seconds = timeout_seconds;
  manager->service_detection = service_detection;
  manager->os_detection = os_detection;
  (void)cmdb_str_copy(manager->job_id, sizeof(manager->job_id), job_id);
  (void)cmdb_str_copy(manager->target, sizeof(manager->target), target);
  (void)cmdb_str_copy(manager->ports, sizeof(manager->ports), ports);
  (void)cmdb_str_copy(manager->status, sizeof(manager->status), "running");
  manager->error[0] = '\0';
#ifdef _WIN32
  manager->thread = CreateThread(NULL, 0, scan_manager_thread_entry,
                                 manager, 0, NULL);
  if (manager->thread == NULL) {
    manager->active = 0;
    scan_manager_unlock(manager);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot start the scan thread");
    return -1;
  }
#else
  if (pthread_create(&manager->thread, NULL, scan_manager_thread_entry,
                     manager) != 0) {
    manager->active = 0;
    scan_manager_unlock(manager);
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot start the scan thread");
    return -1;
  }
#endif
  manager->thread_valid = 1;
  scan_manager_unlock(manager);
  return 0;
}

int cmdb_scan_manager_active(cmdb_scan_manager_t *manager) {
  int active;
  if (manager == NULL) {
    return 0;
  }
  scan_manager_lock(manager);
  active = manager->active;
  scan_manager_unlock(manager);
  return active;
}

int cmdb_scan_manager_cancel(cmdb_scan_manager_t *manager,
                             const char *job_id, cmdb_error_t *err) {
  int found = 0;

  if (manager == NULL || job_id == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid scan cancel");
    return -1;
  }
  scan_manager_lock(manager);
  if (manager->active && strcmp(manager->job_id, job_id) == 0) {
#ifdef _WIN32
    InterlockedExchange((volatile LONG *)&manager->cancel, 1);
#else
    __atomic_store_n(&manager->cancel, 1, __ATOMIC_RELEASE);
#endif
    found = 1;
  }
  scan_manager_unlock(manager);
  if (!found) {
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND,
                   "No running scan matches that job");
    return -1;
  }
  return 0;
}

int cmdb_scan_manager_status(cmdb_scan_manager_t *manager,
                             const char *job_id, char status[16],
                             cmdb_scan_result_t *result, int *has_result,
                             cmdb_error_t *err) {
  int active;

  if (manager == NULL || job_id == NULL || status == NULL || result == NULL ||
      has_result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INVALID_ARGUMENT, "Invalid scan status");
    return -1;
  }
  memset(result, 0, sizeof(*result));
  *has_result = 0;
  scan_manager_lock(manager);
  if (strcmp(manager->job_id, job_id) != 0) {
    scan_manager_unlock(manager);
    cmdb_error_set(err, CMDB_ERR_NOT_FOUND,
                   "No scan manager job matches that ID");
    return -1;
  }
  (void)cmdb_str_copy(status, 16, manager->status);
  active = manager->active;
  if (active) {
    scan_manager_unlock(manager);
    return 0;
  }
  if (strcmp(manager->status, "completed") == 0 && !manager->delivered) {
    manager->delivered = 1;
    *result = manager->result;
    memset(&manager->result, 0, sizeof(manager->result));
    *has_result = 1;
  } else if (strcmp(manager->status, "failed") == 0 ||
             strcmp(manager->status, "cancelled") == 0) {
    (void)cmdb_str_copy(result->error, sizeof(result->error),
                        manager->error);
  }
  scan_manager_unlock(manager);
  scan_manager_join(manager);
  return 0;
}
