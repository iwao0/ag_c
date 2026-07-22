#include "generic_selection_resolution.h"

#include <string.h>

static void initialize_resolution(
    psx_generic_selection_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_GENERIC_SELECTION_RESOLUTION_TYPE_UNRESOLVED;
  resolution->selected_index = -1;
  resolution->conflict_index = -1;
}

static int qual_types_match(
    psx_qual_type_t left, psx_qual_type_t right) {
  return left.type_id == right.type_id &&
         left.qualifiers == right.qualifiers;
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
          qual_types_match(
              association_types[i], association_types[j])) {
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
        qual_types_match(control_type, association_types[i])) {
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
