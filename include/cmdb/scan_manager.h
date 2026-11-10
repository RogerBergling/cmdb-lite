#ifndef CMDB_SCAN_MANAGER_H
#define CMDB_SCAN_MANAGER_H

#include "cmdb/error.h"
#include "cmdb/scan.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cmdb_scan_manager cmdb_scan_manager_t;

cmdb_scan_manager_t *cmdb_scan_manager_new(void);
void cmdb_scan_manager_free(cmdb_scan_manager_t *manager);

int cmdb_scan_manager_start(cmdb_scan_manager_t *manager,
                            const char *job_id, const char *target,
                            const char *ports, int timeout_seconds,
                            int service_detection, int os_detection,
                            cmdb_error_t *err);
int cmdb_scan_manager_active(cmdb_scan_manager_t *manager);
int cmdb_scan_manager_cancel(cmdb_scan_manager_t *manager,
                             const char *job_id, cmdb_error_t *err);
int cmdb_scan_manager_status(cmdb_scan_manager_t *manager,
                             const char *job_id, char status[16],
                             cmdb_scan_result_t *result, int *has_result,
                             cmdb_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_SCAN_MANAGER_H */
