#include "generic_selection_resolution.h"

#include "type_name_resolution.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../parser/node_resolution_state.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type_builder.h"

#include <string.h>

void psx_resolve_generic_selection_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_generic_selection_t *selection,
    psx_generic_selection_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_GENERIC_SELECTION_RESOLUTION_TYPE_UNRESOLVED;
  resolution->selected_index = -1;
  resolution->conflict_index = -1;
  if (!semantic_context || !global_registry || !local_registry ||
      !selection || !selection->control ||
      selection->association_count <= 0)
    return;
  if (!ps_node_prepare_resolution_state_in(
          ps_ctx_arena(semantic_context), (node_t *)selection))
    return;
  psx_generic_selection_resolution_state_t *selection_state =
      &selection->base.resolution_state->generic_selection;
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

  int default_index = -1;
  psx_qual_type_t *association_types = arena_alloc_in(
      ps_ctx_arena(semantic_context),
      (size_t)selection->association_count * sizeof(*association_types));
  if (!association_types) return;
  memset(
      association_types, 0,
      (size_t)selection->association_count * sizeof(*association_types));
  for (int i = 0; i < selection->association_count; i++) {
    psx_generic_association_t *association = &selection->associations[i];
    if (association->is_default) {
      if (default_index >= 0) {
        resolution->status =
            PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_DEFAULT;
        resolution->conflict_index = i;
        return;
      }
      default_index = i;
      continue;
    }
    const psx_type_t *resolved =
        psx_resolve_bound_type_name_ref_in_contexts(
            semantic_context, global_registry, local_registry,
            &association->type_name,
            &selection_state->association_type_names[i]);
    if (resolved) {
      psx_type_t *normalized = ps_type_clone_in(
          ps_ctx_arena(semantic_context), resolved);
      ps_type_normalize_scalar_identity(normalized);
      selection_state->association_type_names[i].resolved_type =
          normalized;
      resolved = normalized;
    }
    if (!resolved) {
      resolution->conflict_index = i;
      return;
    }
    association_types[i] = ps_ctx_intern_qual_type_in(
        semantic_context, resolved);
    if (association_types[i].type_id == PSX_TYPE_ID_INVALID) {
      resolution->conflict_index = i;
      return;
    }
    for (int j = 0; j < i; j++) {
      psx_generic_association_t *previous = &selection->associations[j];
      if (!previous->is_default &&
          association_types[i].type_id == association_types[j].type_id) {
        resolution->status =
            PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_COMPATIBLE_TYPE;
        resolution->conflict_index = i;
        return;
      }
    }
  }

  psx_qual_type_t control_type = ps_node_qual_type(selection->control);
  if (control_type.type_id == PSX_TYPE_ID_INVALID) {
    control_type = ps_ctx_intern_qual_type_in(
        semantic_context, ps_node_get_type(selection->control));
  }
  int selected = -1;
  if (control_type.type_id != PSX_TYPE_ID_INVALID) {
    for (int i = 0; i < selection->association_count; i++) {
      if (!selection->associations[i].is_default &&
          control_type.type_id == association_types[i].type_id) {
        selected = i;
        break;
      }
    }
  }
  if (selected < 0) selected = default_index;
  if (selected < 0) {
    resolution->status = PSX_GENERIC_SELECTION_RESOLUTION_NO_MATCH;
    return;
  }
  const psx_type_t *selected_type = ps_node_get_type(
      selection->associations[selected].expression);
  if (!selected_type) {
    resolution->conflict_index = selected;
    return;
  }
  resolution->status = PSX_GENERIC_SELECTION_RESOLUTION_OK;
  resolution->selected_index = selected;
}

int psx_generic_selection_selected_index(
    const node_generic_selection_t *selection) {
  const psx_generic_selection_resolution_state_t *resolution =
      selection && selection->base.resolution_state
          ? &selection->base.resolution_state->generic_selection : NULL;
  return resolution && resolution->is_resolved
             ? resolution->selected_index : -1;
}

node_t *psx_generic_selection_selected_expression(
    node_generic_selection_t *selection) {
  int selected = psx_generic_selection_selected_index(selection);
  return selection && selected >= 0 &&
                 selected < selection->association_count
             ? selection->associations[selected].expression : NULL;
}

const node_t *psx_generic_selection_selected_expression_const(
    const node_generic_selection_t *selection) {
  int selected = psx_generic_selection_selected_index(selection);
  return selection && selected >= 0 &&
                 selected < selection->association_count
             ? selection->associations[selected].expression : NULL;
}

psx_type_name_resolution_state_t *
psx_generic_selection_type_name_state_mut(
    node_generic_selection_t *selection, int association_index) {
  psx_generic_selection_resolution_state_t *state =
      selection && selection->base.resolution_state
          ? &selection->base.resolution_state->generic_selection : NULL;
  return state && state->association_type_names &&
                 association_index >= 0 &&
                 association_index <
                     state->association_type_name_count
             ? &state->association_type_names[association_index] : NULL;
}

const psx_type_name_resolution_state_t *
psx_generic_selection_type_name_state(
    const node_generic_selection_t *selection, int association_index) {
  const psx_generic_selection_resolution_state_t *state =
      selection && selection->base.resolution_state
          ? &selection->base.resolution_state->generic_selection : NULL;
  return state && state->association_type_names &&
                 association_index >= 0 &&
                 association_index <
                     state->association_type_name_count
             ? &state->association_type_names[association_index] : NULL;
}
