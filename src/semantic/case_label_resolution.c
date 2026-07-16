#include "case_label_resolution.h"

#include "../parser/ast.h"
#include "resolution_state.h"
#include "resolved_node_type.h"

static psx_case_label_resolution_state_t *case_state(
    node_case_t *case_node) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(case_node ? &case_node->base : NULL);
  return state ? &state->case_label : NULL;
}

static const psx_case_label_resolution_state_t *case_state_const(
    const node_case_t *case_node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(case_node ? &case_node->base : NULL);
  return state ? &state->case_label : NULL;
}

void psx_case_label_bind_value(
    node_case_t *case_node, long long value) {
  psx_case_label_resolution_state_t *state = case_state(case_node);
  if (!state) return;
  state->value = value;
  state->is_resolved = 1;
}

int psx_case_label_is_resolved(const node_case_t *case_node) {
  const psx_case_label_resolution_state_t *state =
      case_state_const(case_node);
  return state && state->is_resolved;
}

long long psx_case_label_value(const node_case_t *case_node) {
  const psx_case_label_resolution_state_t *state =
      case_state_const(case_node);
  return state ? state->value : 0;
}
