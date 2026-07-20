#include "generic_selection_resolution.h"

#include "type_name_resolution.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "resolution_state.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"

#include <string.h>

static void initialize_resolution(
    psx_generic_selection_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_GENERIC_SELECTION_RESOLUTION_TYPE_UNRESOLVED;
  resolution->selected_index = -1;
  resolution->conflict_index = -1;
}

void psx_resolve_generic_selection_qual_types_in(
    psx_qual_type_t control_type,
    const psx_qual_type_t *association_types,
    const unsigned char *is_default,
    int association_count,
    psx_generic_selection_resolution_t *resolution) {
  initialize_resolution(resolution);
  if (!resolution || control_type.type_id == PSX_TYPE_ID_INVALID ||
      !association_types || !is_default || association_count <= 0)
    return;

  int default_index = -1;
  for (int i = 0; i < association_count; i++) {
    if (is_default[i]) {
      if (default_index >= 0) {
        resolution->status =
            PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_DEFAULT;
        resolution->conflict_index = i;
        return;
      }
      default_index = i;
      continue;
    }
    if (association_types[i].type_id == PSX_TYPE_ID_INVALID) {
      resolution->conflict_index = i;
      return;
    }
    for (int j = 0; j < i; j++) {
      if (!is_default[j] &&
          association_types[i].type_id ==
              association_types[j].type_id) {
        resolution->status =
            PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_COMPATIBLE_TYPE;
        resolution->conflict_index = i;
        return;
      }
    }
  }

  int selected = -1;
  for (int i = 0; i < association_count; i++) {
    if (!is_default[i] &&
        control_type.type_id == association_types[i].type_id) {
      selected = i;
      break;
    }
  }
  if (selected < 0) selected = default_index;
  if (selected < 0) {
    resolution->status = PSX_GENERIC_SELECTION_RESOLUTION_NO_MATCH;
    return;
  }
  resolution->status = PSX_GENERIC_SELECTION_RESOLUTION_OK;
  resolution->selected_index = selected;
}

void psx_resolve_generic_selection_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_generic_selection_t *selection,
    psx_generic_selection_resolution_t *resolution) {
  if (!resolution) return;
  initialize_resolution(resolution);
  if (!semantic_context || !global_registry || !local_registry ||
      !selection || !selection->control ||
      selection->association_count <= 0)
    return;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  if (!ps_node_prepare_resolution_state_for_size_in(
          store, ps_ctx_arena(semantic_context), (node_t *)selection,
          sizeof(*selection)))
    return;
  psx_node_resolution_state_t *node_state =
      ps_node_resolution_state(store, &selection->base);
  if (!node_state) return;
  psx_generic_selection_resolution_state_t *selection_state =
      &node_state->generic_selection;
  if (!selection_state->association_type_names ||
      selection_state->association_type_name_count !=
          selection->association_count) {
    selection_state->association_type_names = arena_alloc_in(
        ps_ctx_arena(semantic_context),
        (size_t)selection->association_count *
            sizeof(*selection_state->association_type_names));
    if (!selection_state->association_type_names) return;
    memset(
        selection_state->association_type_names, 0,
        (size_t)selection->association_count *
            sizeof(*selection_state->association_type_names));
    selection_state->association_type_name_count =
        selection->association_count;
  }

  psx_qual_type_t *association_types = arena_alloc_in(
      ps_ctx_arena(semantic_context),
      (size_t)selection->association_count * sizeof(*association_types));
  unsigned char *is_default = arena_alloc_in(
      ps_ctx_arena(semantic_context),
      (size_t)selection->association_count * sizeof(*is_default));
  if (!association_types || !is_default) return;
  memset(
      association_types, 0,
      (size_t)selection->association_count * sizeof(*association_types));
  for (int i = 0; i < selection->association_count; i++) {
    psx_generic_association_t *association = &selection->associations[i];
    is_default[i] = association->is_default ? 1 : 0;
    if (association->is_default) {
      continue;
    }
    if (!psx_resolve_bound_type_name_qual_type_in_contexts(
            semantic_context, global_registry, local_registry,
            &association->type_name,
            &selection_state->association_type_names[i],
            &association_types[i])) {
      resolution->conflict_index = i;
      return;
    }
  }

  psx_qual_type_t control_type = ps_node_qual_type(
      store, selection->control);
  if (control_type.type_id == PSX_TYPE_ID_INVALID) return;
  psx_resolve_generic_selection_qual_types_in(
      control_type, association_types, is_default,
      selection->association_count, resolution);
  if (resolution->status != PSX_GENERIC_SELECTION_RESOLUTION_OK)
    return;
  int selected = resolution->selected_index;
  psx_qual_type_t selected_type = ps_node_qual_type(
      store, selection->associations[selected].expression);
  if (selected_type.type_id == PSX_TYPE_ID_INVALID) {
    resolution->status =
        PSX_GENERIC_SELECTION_RESOLUTION_TYPE_UNRESOLVED;
    resolution->conflict_index = selected;
    return;
  }
}

int psx_generic_selection_selected_index(
    const psx_resolution_store_t *store,
    const node_generic_selection_t *selection) {
  const psx_node_resolution_state_t *node_state =
      ps_node_resolution_state_const(
          store, selection ? &selection->base : NULL);
  const psx_generic_selection_resolution_state_t *resolution =
      node_state ? &node_state->generic_selection : NULL;
  return resolution && resolution->is_resolved
             ? resolution->selected_index : -1;
}

node_t *psx_generic_selection_selected_expression(
    const psx_resolution_store_t *store,
    node_generic_selection_t *selection) {
  int selected = psx_generic_selection_selected_index(store, selection);
  return selection && selected >= 0 &&
                 selected < selection->association_count
             ? selection->associations[selected].expression : NULL;
}

const node_t *psx_generic_selection_selected_expression_const(
    const psx_resolution_store_t *store,
    const node_generic_selection_t *selection) {
  int selected = psx_generic_selection_selected_index(store, selection);
  return selection && selected >= 0 &&
                 selected < selection->association_count
             ? selection->associations[selected].expression : NULL;
}

psx_type_name_resolution_state_t *
psx_generic_selection_type_name_state_mut(
    psx_resolution_store_t *store,
    node_generic_selection_t *selection, int association_index) {
  psx_node_resolution_state_t *node_state =
      ps_node_resolution_state(
          store, selection ? &selection->base : NULL);
  psx_generic_selection_resolution_state_t *state =
      node_state ? &node_state->generic_selection : NULL;
  return state && state->association_type_names &&
                 association_index >= 0 &&
                 association_index <
                     state->association_type_name_count
             ? &state->association_type_names[association_index] : NULL;
}

const psx_type_name_resolution_state_t *
psx_generic_selection_type_name_state(
    const psx_resolution_store_t *store,
    const node_generic_selection_t *selection, int association_index) {
  const psx_node_resolution_state_t *node_state =
      ps_node_resolution_state_const(
          store, selection ? &selection->base : NULL);
  const psx_generic_selection_resolution_state_t *state =
      node_state ? &node_state->generic_selection : NULL;
  return state && state->association_type_names &&
                 association_index >= 0 &&
                 association_index <
                     state->association_type_name_count
             ? &state->association_type_names[association_index] : NULL;
}
