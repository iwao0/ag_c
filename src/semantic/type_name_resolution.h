#ifndef PSX_TYPE_NAME_RESOLUTION_H
#define PSX_TYPE_NAME_RESOLUTION_H

#include "../parser/ast.h"
#include "resolution_state.h"
#include "resolved_node_type.h"

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
int psx_resolve_type_name_qual_type_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    psx_qual_type_t *qual_type);
int psx_resolve_bound_type_name_qual_type_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    psx_type_name_resolution_state_t *state,
    psx_qual_type_t *qual_type);
int psx_type_name_bind_resolved_type_in(
    psx_semantic_context_t *semantic_context,
    psx_type_name_resolution_state_t *state,
    const psx_type_t *resolved_type);
const psx_type_t *psx_type_name_bound_base_type(
    const psx_type_name_resolution_state_t *state);
psx_qual_type_t psx_type_name_bound_base_qual_type(
    const psx_type_name_resolution_state_t *state);
const psx_runtime_declarator_application_t *
psx_type_name_bound_runtime_application(
    const psx_type_name_resolution_state_t *state);
const psx_type_t *psx_type_name_resolved_type(
    const psx_type_name_resolution_state_t *state);
psx_qual_type_t psx_type_name_resolved_qual_type(
    const psx_type_name_resolution_state_t *state);

static inline psx_type_name_resolution_state_t *
psx_node_type_name_state_mut(
    psx_resolution_store_t *store, node_t *node) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  return state ? &state->type_name : NULL;
}

static inline const psx_type_name_resolution_state_t *
psx_node_type_name_state(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state ? &state->type_name : NULL;
}

static inline const psx_type_t *psx_node_resolved_type_name(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_type_name_resolution_state_t *state =
      psx_node_type_name_state(store, node);
  return psx_type_name_resolved_type(state);
}

#endif
