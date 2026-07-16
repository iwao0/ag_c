#ifndef SEMANTIC_ALIGNOF_QUERY_RESOLUTION_H
#define SEMANTIC_ALIGNOF_QUERY_RESOLUTION_H

#include "../parser/ast.h"
#include "../parser/node_resolution_state.h"

static inline psx_alignof_query_resolution_state_t *
psx_alignof_query_resolution_state(node_alignof_query_t *query) {
  return query && query->base.resolution_state
             ? &query->base.resolution_state->alignof_query : NULL;
}

static inline const psx_alignof_query_resolution_state_t *
psx_alignof_query_resolution_state_const(
    const node_alignof_query_t *query) {
  return query && query->base.resolution_state
             ? &query->base.resolution_state->alignof_query : NULL;
}

static inline int psx_alignof_query_resolved_alignment(
    const node_alignof_query_t *query) {
  const psx_alignof_query_resolution_state_t *resolution =
      psx_alignof_query_resolution_state_const(query);
  return resolution ? resolution->resolved_alignment : 0;
}

#endif
