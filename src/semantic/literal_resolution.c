#include "literal_resolution.h"

#include "../parser/ast.h"
#include "resolution_state.h"

void psx_string_literal_bind_label(
    node_string_t *literal, char *label) {
  if (!literal || !literal->base.resolution_state) return;
  literal->base.resolution_state->literal.string_label = label;
}

char *psx_string_literal_label(const node_string_t *literal) {
  return literal && literal->base.resolution_state
             ? literal->base.resolution_state->literal.string_label
             : NULL;
}
