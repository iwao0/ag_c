#ifndef SEMANTIC_SOURCE_CAST_RESOLUTION_H
#define SEMANTIC_SOURCE_CAST_RESOLUTION_H

#include "../parser/ast.h"
#include "resolution_state.h"
#include "resolved_node_type.h"

static inline psx_source_cast_resolution_kind_t
psx_source_cast_resolution_kind(
    const psx_resolution_store_t *store,
    const node_source_cast_t *cast) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(
          store, cast ? &cast->base : NULL);
  const psx_source_cast_resolution_t *resolution =
      state ? &state->source_cast : NULL;
  return resolution ? resolution->kind : PSX_SOURCE_CAST_UNRESOLVED;
}

static inline const psx_source_cast_resolution_t *
psx_source_cast_resolution_state(
    const psx_resolution_store_t *store,
    const node_source_cast_t *cast) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(
          store, cast ? &cast->base : NULL);
  return state ? &state->source_cast : NULL;
}

static inline struct lvar_t *psx_source_cast_aggregate_temporary(
    const psx_resolution_store_t *store,
    node_source_cast_t *cast) {
  const psx_source_cast_resolution_t *resolution =
      psx_source_cast_resolution_state(store, cast);
  return resolution &&
                 resolution->kind == PSX_SOURCE_CAST_AGGREGATE_TEMPORARY
             ? resolution->aggregate_temporary : NULL;
}

static inline const struct lvar_t *psx_source_cast_aggregate_temporary_const(
    const psx_resolution_store_t *store,
    const node_source_cast_t *cast) {
  const psx_source_cast_resolution_t *resolution =
      psx_source_cast_resolution_state(store, cast);
  return resolution &&
                 resolution->kind == PSX_SOURCE_CAST_AGGREGATE_TEMPORARY
             ? resolution->aggregate_temporary : NULL;
}

#endif
