#include "generic_selection_resolution.h"

#include "type_name_resolution.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
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

  int default_index = -1;
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
            &association->type_name);
    if (resolved) {
      psx_type_t *normalized = ps_type_clone(resolved);
      ps_type_normalize_integer_identity(normalized);
      association->type_name.resolved_type = normalized;
      resolved = normalized;
    }
    if (!resolved) {
      resolution->conflict_index = i;
      return;
    }
    for (int j = 0; j < i; j++) {
      psx_generic_association_t *previous = &selection->associations[j];
      if (!previous->is_default &&
          ps_type_generic_matches(
              resolved, previous->type_name.resolved_type)) {
        resolution->status =
            PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_COMPATIBLE_TYPE;
        resolution->conflict_index = i;
        return;
      }
    }
  }

  int selected = ps_node_generic_selection_index(selection);
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
