#include "resolved_node_type.h"

#include "../parser/arena.h"
#include "../parser/ast.h"
#include "resolution_state.h"

const psx_type_t *ps_node_get_type(const node_t *node) {
  return node && node->resolution_state
             ? node->resolution_state->type : NULL;
}

psx_qual_type_t ps_node_qual_type(const node_t *node) {
  return node && node->resolution_state
             ? node->resolution_state->qual_type
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
}

int ps_node_prepare_resolution_state_in(
    arena_context_t *arena_context, node_t *node) {
  if (!node || !arena_context) return 0;
  if (node->resolution_state) return 1;
  node->resolution_state = arena_alloc_in(
      arena_context, sizeof(*node->resolution_state));
  return node->resolution_state != NULL;
}

int ps_node_copy_resolution_state_in(
    arena_context_t *arena_context, node_t *destination,
    const node_t *source) {
  if (!destination) return 0;
  if (!source || !source->resolution_state) {
    if (destination->resolution_state)
      *destination->resolution_state =
          (psx_node_resolution_state_t){0};
    return 1;
  }
  if (!ps_node_prepare_resolution_state_in(
          arena_context, destination))
    return 0;
  *destination->resolution_state = *source->resolution_state;
  return 1;
}
