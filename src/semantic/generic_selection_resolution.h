#ifndef SEMANTIC_GENERIC_SELECTION_RESOLUTION_H
#define SEMANTIC_GENERIC_SELECTION_RESOLUTION_H

#include "../type_system/type_ids.h"

typedef enum {
  PSX_GENERIC_SELECTION_RESOLUTION_OK = 0,
  PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_DEFAULT,
  PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_COMPATIBLE_TYPE,
  PSX_GENERIC_SELECTION_RESOLUTION_NO_MATCH,
  PSX_GENERIC_SELECTION_RESOLUTION_TYPE_UNRESOLVED,
} psx_generic_selection_resolution_status_t;

typedef struct {
  psx_generic_selection_resolution_status_t status;
  int selected_index;
  int conflict_index;
} psx_generic_selection_resolution_t;

void psx_resolve_generic_selection_qual_types_in(
    psx_qual_type_t control_type,
    const psx_qual_type_t *association_types,
    const unsigned char *is_default,
    int association_count,
    psx_generic_selection_resolution_t *resolution);

#endif
