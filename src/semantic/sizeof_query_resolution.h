#ifndef SEMANTIC_SIZEOF_QUERY_RESOLUTION_H
#define SEMANTIC_SIZEOF_QUERY_RESOLUTION_H

#include "../parser/ast.h"
#include "resolution_state.h"
#include "resolved_node_type.h"

typedef struct psx_sizeof_runtime_plan_t {
  node_t **runtime_bounds;
  int runtime_bound_count;
  long long constant_factor;
} psx_sizeof_runtime_plan_t;

static inline psx_sizeof_query_resolution_state_t *
psx_sizeof_query_resolution_state(node_sizeof_query_t *query) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(query ? &query->base : NULL);
  return state ? &state->sizeof_query : NULL;
}

static inline const psx_sizeof_query_resolution_state_t *
psx_sizeof_query_resolution_state_const(
    const node_sizeof_query_t *query) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(query ? &query->base : NULL);
  return state ? &state->sizeof_query : NULL;
}

static inline int psx_sizeof_query_resolved_size(
    const node_sizeof_query_t *query) {
  const psx_sizeof_query_resolution_state_t *resolution =
      psx_sizeof_query_resolution_state_const(query);
  return resolution ? resolution->resolved_size : 0;
}

static inline int psx_sizeof_query_runtime_size_slot(
    const node_sizeof_query_t *query) {
  const psx_sizeof_query_resolution_state_t *resolution =
      psx_sizeof_query_resolution_state_const(query);
  return resolution ? resolution->runtime_size_slot : 0;
}

static inline int psx_sizeof_query_evaluates_vla_operand(
    const node_sizeof_query_t *query) {
  const psx_sizeof_query_resolution_state_t *resolution =
      psx_sizeof_query_resolution_state_const(query);
  return resolution && resolution->evaluates_vla_operand;
}

static inline psx_sizeof_runtime_plan_t *
psx_sizeof_query_runtime_plan(node_sizeof_query_t *query) {
  psx_sizeof_query_resolution_state_t *resolution =
      psx_sizeof_query_resolution_state(query);
  return resolution ? resolution->runtime_plan : NULL;
}

static inline const psx_sizeof_runtime_plan_t *
psx_sizeof_query_runtime_plan_const(
    const node_sizeof_query_t *query) {
  const psx_sizeof_query_resolution_state_t *resolution =
      psx_sizeof_query_resolution_state_const(query);
  return resolution ? resolution->runtime_plan : NULL;
}

#endif
