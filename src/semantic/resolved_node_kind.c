#include "resolved_node_kind.h"

#include "../parser/ast.h"
#include "resolved_node_type.h"
#include "resolution_state.h"

psx_resolution_node_kind_t psx_resolution_node_kind(
    const node_t *node) {
  if (!node) return PSX_SYNTAX_NODE_INVALID;
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(node);
  if (state) {
    if (state->node_kind != PSX_RESOLVED_NODE_INVALID)
      return (psx_resolution_node_kind_t)state->node_kind;
    switch (state->reference.kind) {
      case PSX_RESOLVED_REFERENCE_LOCAL: return ND_LVAR;
      case PSX_RESOLVED_REFERENCE_GLOBAL: return ND_GVAR;
      case PSX_RESOLVED_REFERENCE_FUNCTION: return ND_FUNCREF;
      case PSX_RESOLVED_REFERENCE_VA_ARG_AREA: return ND_VA_ARG_AREA;
      case PSX_RESOLVED_REFERENCE_NONE: break;
    }
  }
  return (psx_resolution_node_kind_t)node->kind;
}

int psx_resolution_node_set_kind(
    node_t *node, psx_resolved_node_kind_t kind) {
  if (!node || kind == PSX_RESOLVED_NODE_INVALID) return 0;
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(node);
  if (!state) return 0;
  state->node_kind = kind;
  node->kind = PSX_SYNTAX_NODE_INVALID;
  return 1;
}
