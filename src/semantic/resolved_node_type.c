#include "resolved_node_type.h"

#include <stddef.h>

#include "../parser/arena.h"
#include "../parser/ast.h"
#include "../parser/node_vla_public.h"
#include "resolved_node_kind.h"
#include "resolution_state.h"
#include "resolution_store.h"
#include "type_identity.h"

void *psx_resolution_node_alloc_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    size_t node_size) {
  if (!arena_context || node_size < sizeof(node_t)) return NULL;
  node_t *node = arena_alloc_in(arena_context, node_size);
  return node && psx_resolution_store_prepare_in(
                     store, arena_context, node, node_size)
             ? node : NULL;
}

psx_node_resolution_state_t *ps_node_resolution_state(
    psx_resolution_store_t *store, node_t *node) {
  return psx_resolution_store_lookup(store, node);
}

const psx_node_resolution_state_t *ps_node_resolution_state_const(
    const psx_resolution_store_t *store, const node_t *node) {
  return psx_resolution_store_lookup_const(store, node);
}

int ps_node_has_resolution_state(
    const psx_resolution_store_t *store, const node_t *node) {
  return ps_node_resolution_state_const(store, node) != NULL;
}

size_t psx_resolution_node_storage_size(
    const psx_resolution_store_t *store, const node_t *node) {
  return psx_resolution_store_node_size(store, node);
}

psx_qual_type_t ps_node_qual_type(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state && state->type_binding.kind == PSX_NODE_TYPE_CANONICAL
             ? state->type_binding.canonical_type
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
}

int ps_node_type_shape(
    const psx_resolution_store_t *store, const node_t *node,
    psx_type_shape_t *shape) {
  psx_qual_type_t type = ps_node_qual_type(store, node);
  return shape && type.type_id != PSX_TYPE_ID_INVALID &&
         psx_semantic_type_table_describe(
             psx_resolution_store_semantic_types(store),
             type.type_id, shape);
}

static int bound_type_accepts_vla_runtime_view(
    const psx_resolution_store_t *store, const node_t *node) {
  psx_qual_type_t type = ps_node_qual_type(store, node);
  const psx_semantic_type_table_t *types =
      psx_resolution_store_semantic_types(store);
  return psx_semantic_type_table_qual_type_is_valid(types, type) &&
         psx_semantic_type_table_contains_vla_array(types, type.type_id);
}

static node_t *bound_type_vla_runtime_source(
    const psx_resolution_store_t *store, node_t *node) {
  if (!node) return NULL;
  if (psx_resolution_node_kind(store, node) == ND_ADDR)
    return node->lhs;
  switch (psx_resolution_node_kind(store, node)) {
    case ND_ADD:
      if (node->lhs &&
          ps_node_vla_row_stride_frame_off(store, node->lhs) != 0)
        return node->lhs;
      return node->rhs;
    case ND_SUB:
    case ND_ASSIGN:
    case ND_COMPOUND_ASSIGN:
    case ND_CAST:
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return node->lhs;
    case ND_COMMA:
    case ND_STMT_EXPR:
    case ND_TERNARY:
      return node->rhs;
    default:
      return NULL;
  }
}

static void refresh_bound_type_vla_runtime(
    psx_resolution_store_t *store, node_t *node) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (!state) return;
  if (!bound_type_accepts_vla_runtime_view(store, node)) {
    state->expr.vla_runtime = (psx_vla_runtime_view_t){0};
    return;
  }
  node_t *source = bound_type_vla_runtime_source(store, node);
  if (source) {
    ps_node_set_vla_runtime_view(
        store, node,
        ps_node_vla_row_stride_frame_off(store, source),
        ps_node_vla_strides_remaining(store, source));
  } else if (psx_resolution_node_kind(store, node) == ND_DEREF &&
             node->lhs) {
    int frame_off = ps_node_vla_row_stride_frame_off(store, node->lhs);
    int remaining = ps_node_vla_strides_remaining(store, node->lhs);
    ps_node_set_vla_runtime_view(
        store, node, frame_off != 0 && remaining > 0 ? frame_off + 8 : 0,
        remaining > 0 ? remaining - 1 : 0);
  }
}

int ps_node_bind_qual_type(
    psx_resolution_store_t *store, node_t *node,
    psx_qual_type_t qual_type) {
  if (!node || qual_type.type_id == PSX_TYPE_ID_INVALID) return 0;
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (!state || !psx_semantic_type_table_qual_type_is_valid(
                    psx_resolution_store_semantic_types(store),
                    qual_type))
    return 0;
  state->type_binding = (psx_node_type_binding_t){
      .kind = PSX_NODE_TYPE_CANONICAL,
      .canonical_type = qual_type,
  };
  refresh_bound_type_vla_runtime(store, node);
  return 1;
}

void ps_node_clear_type(psx_resolution_store_t *store, node_t *node) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (!state) return;
  state->type_binding = (psx_node_type_binding_t){0};
  state->expr.vla_runtime = (psx_vla_runtime_view_t){0};
}

int ps_node_prepare_resolution_state_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *node) {
  return ps_node_prepare_resolution_state_for_size_in(
      store, arena_context, node, sizeof(*node));
}

int ps_node_prepare_resolution_state_for_size_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *node, size_t node_size) {
  return psx_resolution_store_prepare_in(
      store, arena_context, node, node_size);
}

int ps_node_copy_resolution_state_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *destination, const node_t *source) {
  if (!destination) return 0;
  const psx_node_resolution_state_t *source_state =
      ps_node_resolution_state_const(store, source);
  psx_node_resolution_state_t *destination_state =
      ps_node_resolution_state(store, destination);
  if (!source_state) {
    if (destination_state)
      *destination_state = (psx_node_resolution_state_t){0};
    return 1;
  }
  if (!ps_node_prepare_resolution_state_in(
          store, arena_context, destination))
    return 0;
  destination_state = ps_node_resolution_state(store, destination);
  *destination_state = *source_state;
  return 1;
}
