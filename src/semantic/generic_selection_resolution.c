#include "generic_selection_resolution.h"

#include "type_name_resolution.h"
#include "../parser/node_utils.h"

#include <string.h>

void psx_resolve_generic_selection(
    node_generic_selection_t *selection,
    psx_generic_selection_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_GENERIC_SELECTION_RESOLUTION_TYPE_UNRESOLVED;
  resolution->selected_index = -1;
  resolution->conflict_index = -1;
  if (!selection || !selection->control ||
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
    if (!association->type)
      association->type =
          psx_resolve_bound_type_name_ref(&association->type_name);
    if (!association->type) {
      resolution->conflict_index = i;
      return;
    }
    ps_type_normalize_integer_identity(association->type);
    for (int j = 0; j < i; j++) {
      psx_generic_association_t *previous = &selection->associations[j];
      if (!previous->is_default &&
          ps_type_generic_matches(association->type, previous->type)) {
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
  psx_type_t *selected_type = ps_node_get_type(
      selection->associations[selected].expression);
  if (!selected_type) {
    resolution->conflict_index = selected;
    return;
  }
  resolution->status = PSX_GENERIC_SELECTION_RESOLUTION_OK;
  resolution->selected_index = selected;
  resolution->result_type = ps_type_clone(selected_type);
}
