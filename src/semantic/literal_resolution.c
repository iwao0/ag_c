#include "literal_resolution.h"

#include "../parser/ast.h"
#include "resolution_state.h"
#include "resolved_node_type.h"

void psx_string_literal_bind_label(
    node_string_t *literal, char *label) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(literal ? &literal->base : NULL);
  if (state) state->literal.string_label = label;
}

char *psx_string_literal_label(const node_string_t *literal) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(literal ? &literal->base : NULL);
  return state ? state->literal.string_label : NULL;
}
