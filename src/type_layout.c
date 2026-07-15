#include "type_layout.h"

#include "parser/type.h"

#include <limits.h>
#include <string.h>

static ag_target_scalar_kind_t integer_target_kind(
    const psx_type_t *type) {
  if (type && type->is_long_long) return AG_TARGET_SCALAR_LONG_LONG;
  switch (type ? type->scalar_kind : TK_INT) {
    case TK_CHAR: return AG_TARGET_SCALAR_CHAR;
    case TK_SHORT: return AG_TARGET_SCALAR_SHORT;
    case TK_LONG: return AG_TARGET_SCALAR_LONG;
    default: return AG_TARGET_SCALAR_INT;
  }
}

static ag_target_scalar_kind_t floating_target_kind(
    const psx_type_t *type) {
  int is_complex = type && type->kind == PSX_TYPE_COMPLEX;
  if (type && (type->is_long_double ||
               type->fp_kind == TK_FLOAT_KIND_LONG_DOUBLE)) {
    return is_complex ? AG_TARGET_SCALAR_LONG_DOUBLE_COMPLEX
                      : AG_TARGET_SCALAR_LONG_DOUBLE;
  }
  if (type && type->fp_kind == TK_FLOAT_KIND_FLOAT)
    return is_complex ? AG_TARGET_SCALAR_FLOAT_COMPLEX
                      : AG_TARGET_SCALAR_FLOAT;
  return is_complex ? AG_TARGET_SCALAR_DOUBLE_COMPLEX
                    : AG_TARGET_SCALAR_DOUBLE;
}

static int layout_scalar(
    const psx_type_t *type, const ag_target_info_t *target,
    psx_type_layout_t *out) {
  ag_target_scalar_kind_t kind;
  if (type->kind == PSX_TYPE_BOOL) {
    kind = AG_TARGET_SCALAR_CHAR;
  } else if (type->kind == PSX_TYPE_INTEGER) {
    kind = integer_target_kind(type);
  } else {
    kind = floating_target_kind(type);
  }
  out->size = ag_target_info_scalar_size(target, kind);
  out->alignment = ag_target_info_scalar_alignment(target, kind);
  out->is_complete = 1;
  return 1;
}

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
      out->alignment = ag_target_info_pointer_alignment(target);
      out->is_complete = 1;
      return 1;
    case PSX_TYPE_ARRAY:
      return 0;
    case PSX_TYPE_BOOL:
    case PSX_TYPE_INTEGER:
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_COMPLEX:
      return layout_scalar(type, target, out);
    case PSX_TYPE_STRUCT:
    case PSX_TYPE_UNION:
      out->is_complete = type->aggregate_definition &&
                         type->aggregate_definition->is_complete;
      return 1;
    default:
      return 0;
  }
}

static int layout_non_array_with_records(
    const psx_type_t *type,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_layout_t *out) {
  if (!type || !out) return 0;
  if (type->kind != PSX_TYPE_STRUCT && type->kind != PSX_TYPE_UNION)
    return layout_non_array(type, target, out);
  memset(out, 0, sizeof(*out));
  out->alignment = 1;
  if (type->record_id == PSX_RECORD_ID_INVALID) return 1;
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      record_layouts, type->record_id, target);
  if (!layout) return 1;
  out->size = layout->size;
  out->alignment = layout->alignment;
  out->is_complete = 1;
  return 1;
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

static int layout_of_id_with_records(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_target_info_t *target,
    psx_type_layout_t *out) {
  const psx_type_t *type = psx_semantic_type_table_lookup(types, type_id);
  if (!type || !out) return 0;
  if (type->kind != PSX_TYPE_ARRAY)
    return layout_non_array_with_records(
        type, record_layouts, target, out);
  psx_type_id_t element_type_id = psx_semantic_type_table_base(
      types, type_id).type_id;
  psx_type_layout_t element = {0};
  if (!layout_of_id_with_records(
          types, record_layouts, element_type_id, target, &element))
    return 0;
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

int ps_type_layout_of_id_with_records(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_target_info_t *target,
    psx_type_layout_t *out) {
  if (!types || !record_layouts || !target || !out) return 0;
  return layout_of_id_with_records(
      types, record_layouts, type_id, target, out);
}

int ps_type_sizeof_id_with_records(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_target_info_t *target) {
  psx_type_layout_t layout = {0};
  return ps_type_layout_of_id_with_records(
             types, record_layouts, type_id, target, &layout) &&
                 layout.is_complete
             ? layout.size
             : 0;
}

int ps_type_alignof_id_with_records(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_target_info_t *target) {
  psx_type_layout_t layout = {0};
  return ps_type_layout_of_id_with_records(
             types, record_layouts, type_id, target, &layout)
             ? layout.alignment
             : 0;
}
