#ifndef SEMANTIC_COMPOUND_LITERAL_RESOLUTION_H
#define SEMANTIC_COMPOUND_LITERAL_RESOLUTION_H

#include "../parser/ast.h"
#include "resolution_state.h"
#include "resolved_node_type.h"

static inline psx_compound_literal_resolution_kind_t
psx_compound_literal_resolution_kind(
    const psx_resolution_store_t *store,
    const node_compound_literal_t *compound) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(
          store, compound ? &compound->base : NULL);
  const psx_compound_literal_resolution_t *resolution =
      state ? &state->compound_literal : NULL;
  return resolution ? resolution->kind
                    : PSX_COMPOUND_LITERAL_UNPLANNED;
}

static inline int psx_compound_literal_is_planned(
    const psx_resolution_store_t *store,
    const node_compound_literal_t *compound) {
  return psx_compound_literal_resolution_kind(store, compound) !=
         PSX_COMPOUND_LITERAL_UNPLANNED;
}

static inline node_t *psx_compound_literal_direct_initializer(
    const psx_resolution_store_t *store,
    node_compound_literal_t *compound) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(
          store, compound ? &compound->base : NULL);
  const psx_compound_literal_resolution_t *resolution =
      state ? &state->compound_literal : NULL;
  node_init_list_t *list =
      compound && compound->base.rhs &&
              compound->base.rhs->kind == ND_INIT_LIST
          ? (node_init_list_t *)compound->base.rhs : NULL;
  int index = resolution ? resolution->direct_initializer_index : -1;
  return resolution &&
                 resolution->kind ==
                     PSX_COMPOUND_LITERAL_DIRECT_INITIALIZER &&
                 list && index >= 0 && index < list->entry_count
             ? list->entries[index].value : NULL;
}

static inline const node_t *
psx_compound_literal_direct_initializer_const(
    const psx_resolution_store_t *store,
    const node_compound_literal_t *compound) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(
          store, compound ? &compound->base : NULL);
  const psx_compound_literal_resolution_t *resolution =
      state ? &state->compound_literal : NULL;
  const node_init_list_t *list =
      compound && compound->base.rhs &&
              compound->base.rhs->kind == ND_INIT_LIST
          ? (const node_init_list_t *)compound->base.rhs : NULL;
  int index = resolution ? resolution->direct_initializer_index : -1;
  return resolution &&
                 resolution->kind ==
                     PSX_COMPOUND_LITERAL_DIRECT_INITIALIZER &&
                 list && index >= 0 && index < list->entry_count
             ? list->entries[index].value : NULL;
}

#endif
