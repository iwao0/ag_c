#include "resolved_node_type.h"

#include <stddef.h>

#include "../parser/arena.h"
#include "../parser/ast.h"
#include "resolution_state.h"
#include "resolution_store.h"

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

psx_lvar_usage_region_t *ps_node_lvar_usage_region(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state ? state->lvar_usage.region : NULL;
}

void ps_node_set_lvar_usage_region(
    psx_resolution_store_t *store, node_t *node,
    psx_lvar_usage_region_t *region) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (state) state->lvar_usage.region = region;
}

lvar_t *ps_node_lvar_usage_symbol(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state ? state->lvar_usage.local : NULL;
}

int ps_node_records_lvar_usage(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state && state->lvar_usage.records_usage;
}

void ps_node_record_lvar_usage(
    psx_resolution_store_t *store, node_t *node, lvar_t *local) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (!state) return;
  state->lvar_usage.local = local;
  state->lvar_usage.records_usage = local ? 1 : 0;
}

int ps_node_is_decl_initializer(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state && state->flags.is_decl_initializer;
}

void ps_node_set_decl_initializer(
    psx_resolution_store_t *store, node_t *node, int enabled) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (state) state->flags.is_decl_initializer = enabled ? 1 : 0;
}

int ps_node_is_implicit_int_return(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state && state->flags.is_implicit_int_return;
}

void ps_node_set_implicit_int_return(
    psx_resolution_store_t *store, node_t *node, int enabled) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (state) state->flags.is_implicit_int_return = enabled ? 1 : 0;
}

int ps_node_widen_zext_i64(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state && state->flags.widen_zext_i64;
}

void ps_node_set_widen_zext_i64(
    psx_resolution_store_t *store, node_t *node, int enabled) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (state) state->flags.widen_zext_i64 = enabled ? 1 : 0;
}

size_t psx_resolution_node_storage_size(
    const psx_resolution_store_t *store, const node_t *node) {
  return psx_resolution_store_node_size(store, node);
}

const psx_type_t *ps_node_get_type(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state ? state->type : NULL;
}

psx_qual_type_t ps_node_qual_type(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  return state ? state->qual_type
               : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                   PSX_TYPE_QUALIFIER_NONE};
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
