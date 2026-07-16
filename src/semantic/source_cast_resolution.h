#ifndef SEMANTIC_SOURCE_CAST_RESOLUTION_H
#define SEMANTIC_SOURCE_CAST_RESOLUTION_H

#include "../parser/ast.h"
#include "resolution_state.h"

static inline psx_source_cast_resolution_kind_t
psx_source_cast_resolution_kind(
    const node_source_cast_t *cast) {
  const psx_source_cast_resolution_t *resolution =
      cast && cast->base.resolution_state
          ? &cast->base.resolution_state->source_cast : NULL;
  return resolution ? resolution->kind : PSX_SOURCE_CAST_UNRESOLVED;
}

static inline const psx_source_cast_resolution_t *
psx_source_cast_resolution_state(
    const node_source_cast_t *cast) {
  return cast && cast->base.resolution_state
             ? &cast->base.resolution_state->source_cast : NULL;
}

static inline struct lvar_t *psx_source_cast_aggregate_temporary(
    node_source_cast_t *cast) {
  const psx_source_cast_resolution_t *resolution =
      cast && cast->base.resolution_state
          ? &cast->base.resolution_state->source_cast : NULL;
  return resolution &&
                 resolution->kind == PSX_SOURCE_CAST_AGGREGATE_TEMPORARY
             ? resolution->aggregate_temporary : NULL;
}

static inline const struct lvar_t *psx_source_cast_aggregate_temporary_const(
    const node_source_cast_t *cast) {
  const psx_source_cast_resolution_t *resolution =
      cast && cast->base.resolution_state
          ? &cast->base.resolution_state->source_cast : NULL;
  return resolution &&
                 resolution->kind == PSX_SOURCE_CAST_AGGREGATE_TEMPORARY
             ? resolution->aggregate_temporary : NULL;
}

#endif
