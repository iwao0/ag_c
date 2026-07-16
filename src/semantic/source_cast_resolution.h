#ifndef SEMANTIC_SOURCE_CAST_RESOLUTION_H
#define SEMANTIC_SOURCE_CAST_RESOLUTION_H

#include "../parser/ast.h"
#include "../parser/node_resolution_state.h"

static inline node_t *psx_source_cast_lowered_value(
    node_source_cast_t *cast) {
  psx_source_cast_resolution_t *resolution =
      cast && cast->base.resolution_state
          ? &cast->base.resolution_state->source_cast : NULL;
  return resolution && resolution->is_lowered
             ? resolution->lowered_value : NULL;
}

static inline const node_t *psx_source_cast_lowered_value_const(
    const node_source_cast_t *cast) {
  const psx_source_cast_resolution_t *resolution =
      cast && cast->base.resolution_state
          ? &cast->base.resolution_state->source_cast : NULL;
  return resolution && resolution->is_lowered
             ? resolution->lowered_value : NULL;
}

#endif
