#ifndef CMDB_DIAGRAM_H
#define CMDB_DIAGRAM_H

#include "cmdb/db.h"
#include "cmdb/error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CMDB_MAX_DIAGRAM_XML (4u * 1024u * 1024u)
#define CMDB_MAX_DIAGRAM_NAME 256

typedef struct {
  char id[37];
  char name[CMDB_MAX_DIAGRAM_NAME];
  char *drawio_xml;
  char created_at[21];
  char updated_at[21];
} cmdb_diagram_t;

int cmdb_diagram_list(cmdb_db_t *db, cmdb_diagram_t **out, size_t *count,
                      int include_xml, cmdb_error_t *err);
int cmdb_diagram_get(cmdb_db_t *db, const char *id, cmdb_diagram_t *out,
                     cmdb_error_t *err);
int cmdb_diagram_save(cmdb_db_t *db, const char *id, const char *name,
                      const char *xml, char out_id[37], cmdb_error_t *err);
int cmdb_diagram_delete(cmdb_db_t *db, const char *id, cmdb_error_t *err);
void cmdb_diagram_free(cmdb_diagram_t *diagram);
void cmdb_diagram_free_list(cmdb_diagram_t *diagrams, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* CMDB_DIAGRAM_H */
