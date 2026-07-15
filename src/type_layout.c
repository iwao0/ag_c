#include "type_layout.h"

#include "parser/type.h"

#include <limits.h>
#include <string.h>

static int layout_non_array(
    const psx_type_t *type, const ag_target_info_t *target,
    psx_type_layout_t *out) {
  if (!type || !out) return 0;
  memset(out, 0, sizeof(*out));
  out->alignment = 1;

  switch (type->kind) {
    case PSX_TYPE_VOID:
    case PSX_TYPE_FUNCTION:
    case PSX_TYPE_INVALID:
      return 1;
    case PSX_TYPE_POINTER:
      out->size = ag_target_info_pointer_size(target);
      out->alignment = out->size;
      out->is_complete = 1;
      return 1;
    case PSX_TYPE_ARRAY:
      return 0;
    case PSX_TYPE_STRUCT:
    case PSX_TYPE_UNION:
      if (type->aggregate_definition) {
        out->size = type->aggregate_definition->size;
        out->alignment = type->aggregate_definition->align > 0
                             ? type->aggregate_definition->align
                             : 1;
        out->is_complete = type->aggregate_definition->is_complete;
        return 1;
      }
      out->size = type->size;
      out->alignment = type->align > 0 ? type->align : 1;
      out->is_complete = type->size > 0;
      return 1;
    default:
      out->size = type->size;
      out->alignment = type->align > 0 ? type->align : 1;
      out->is_complete = type->size > 0;
      return 1;
  }
}

static int complete_array_layout(
    const psx_type_t *array_type, const psx_type_layout_t *element,
    psx_type_layout_t *out) {
  if (!array_type || !element || !out) return 0;
  memset(out, 0, sizeof(*out));
  out->alignment = element->alignment;
  if (!element->is_complete || array_type->is_vla ||
      array_type->array_len <= 0)
    return 1;
  if (element->size > 0 &&
      array_type->array_len > INT_MAX / element->size)
    return 0;
  out->size = array_type->array_len * element->size;
  out->is_complete = 1;
  return 1;
}

static int layout_of(
    const psx_type_t *type, const ag_target_info_t *target,
    psx_type_layout_t *out) {
  if (!type || !out) return 0;
  if (type->kind != PSX_TYPE_ARRAY)
    return layout_non_array(type, target, out);
  psx_type_layout_t element = {0};
  if (!layout_of(type->base, target, &element)) return 0;
  return complete_array_layout(type, &element, out);
}

static int layout_of_id(
    const psx_semantic_type_table_t *types, psx_type_id_t type_id,
    const ag_target_info_t *target, psx_type_layout_t *out) {
  const psx_type_t *type = psx_semantic_type_table_lookup(types, type_id);
  if (!type || !out) return 0;
  if (type->kind != PSX_TYPE_ARRAY)
    return layout_non_array(type, target, out);
  psx_type_id_t element_type_id = psx_semantic_type_table_base(
      types, type_id).type_id;
  psx_type_layout_t element = {0};
  if (!layout_of_id(types, element_type_id, target, &element)) return 0;
  return complete_array_layout(type, &element, out);
}

int ps_type_layout_of(
    const psx_type_t *type, const ag_target_info_t *target,
    psx_type_layout_t *out) {
  return layout_of(type, target, out);
}

int ps_type_sizeof_for_target(
    const psx_type_t *type, const ag_target_info_t *target) {
  psx_type_layout_t layout = {0};
  return ps_type_layout_of(type, target, &layout) && layout.is_complete
             ? layout.size
             : 0;
}

int ps_type_alignof_for_target(
    const psx_type_t *type, const ag_target_info_t *target) {
  psx_type_layout_t layout = {0};
  return ps_type_layout_of(type, target, &layout)
             ? layout.alignment
             : 0;
}

int ps_type_layout_of_id(
    const psx_semantic_type_table_t *types, psx_type_id_t type_id,
    const ag_target_info_t *target, psx_type_layout_t *out) {
  return layout_of_id(types, type_id, target, out);
}

int ps_type_sizeof_id_for_target(
    const psx_semantic_type_table_t *types, psx_type_id_t type_id,
    const ag_target_info_t *target) {
  psx_type_layout_t layout = {0};
  return ps_type_layout_of_id(types, type_id, target, &layout) &&
                 layout.is_complete
             ? layout.size
             : 0;
}

int ps_type_alignof_id_for_target(
    const psx_semantic_type_table_t *types, psx_type_id_t type_id,
    const ag_target_info_t *target) {
  psx_type_layout_t layout = {0};
  return ps_type_layout_of_id(types, type_id, target, &layout)
             ? layout.alignment
             : 0;
}
