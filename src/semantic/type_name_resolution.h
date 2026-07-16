#ifndef PSX_TYPE_NAME_RESOLUTION_H
#define PSX_TYPE_NAME_RESOLUTION_H

#include "../parser/ast.h"
#include "resolution_state.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;

int psx_bind_type_name_ref_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    psx_type_name_resolution_state_t *state);
const psx_type_t *psx_resolve_bound_type_name_ref_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    psx_type_name_resolution_state_t *state);

static inline psx_type_name_resolution_state_t *
psx_node_type_name_state_mut(node_t *node) {
  return node && node->resolution_state
             ? &node->resolution_state->type_name : NULL;
}

static inline const psx_type_name_resolution_state_t *
psx_node_type_name_state(const node_t *node) {
  return node && node->resolution_state
             ? &node->resolution_state->type_name : NULL;
}

static inline const psx_type_t *psx_node_resolved_type_name(
    const node_t *node) {
  const psx_type_name_resolution_state_t *state =
      psx_node_type_name_state(node);
  return state ? state->resolved_type : NULL;
}

#endif
