#ifndef SEMANTIC_ALIGNOF_QUERY_RESOLUTION_H
#define SEMANTIC_ALIGNOF_QUERY_RESOLUTION_H

#include "../parser/ast.h"
#include "resolution_state.h"
#include "resolved_node_type.h"

static inline psx_alignof_query_resolution_state_t *
psx_alignof_query_resolution_state(
    psx_resolution_store_t *store, node_alignof_query_t *query) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, query ? &query->base : NULL);
  return state ? &state->alignof_query : NULL;
}

static inline const psx_alignof_query_resolution_state_t *
psx_alignof_query_resolution_state_const(
    const psx_resolution_store_t *store,
    const node_alignof_query_t *query) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(
          store, query ? &query->base : NULL);
  return state ? &state->alignof_query : NULL;
}

static inline int psx_alignof_query_resolved_alignment(
    const psx_resolution_store_t *store,
    const node_alignof_query_t *query) {
  const psx_alignof_query_resolution_state_t *resolution =
      psx_alignof_query_resolution_state_const(store, query);
  return resolution ? resolution->resolved_alignment : 0;
}

#endif
