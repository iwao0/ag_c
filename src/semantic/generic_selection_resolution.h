#ifndef SEMANTIC_GENERIC_SELECTION_RESOLUTION_H
#define SEMANTIC_GENERIC_SELECTION_RESOLUTION_H

#include "../parser/ast.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;

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

void psx_resolve_generic_selection_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_generic_selection_t *selection,
    psx_generic_selection_resolution_t *resolution);

int psx_generic_selection_selected_index(
    const node_generic_selection_t *selection);
node_t *psx_generic_selection_selected_expression(
    node_generic_selection_t *selection);
const node_t *psx_generic_selection_selected_expression_const(
    const node_generic_selection_t *selection);

#endif
