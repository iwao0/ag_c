#include "type_layout.h"

#include <limits.h>
#include <string.h>

static ag_target_scalar_kind_t integer_target_kind(
    const psx_type_shape_t *type) {
  switch (type ? type->integer_kind : PSX_INTEGER_KIND_INT) {
    case PSX_INTEGER_KIND_CHAR: return AG_TARGET_SCALAR_CHAR;
    case PSX_INTEGER_KIND_SHORT: return AG_TARGET_SCALAR_SHORT;
    case PSX_INTEGER_KIND_LONG: return AG_TARGET_SCALAR_LONG;
    case PSX_INTEGER_KIND_LONG_LONG: return AG_TARGET_SCALAR_LONG_LONG;
    default: return AG_TARGET_SCALAR_INT;
  }
}

static ag_target_scalar_kind_t floating_target_kind(
    const psx_type_shape_t *type) {
  int is_complex = type && type->kind == PSX_TYPE_COMPLEX;
  if (type && type->floating_kind == PSX_FLOATING_KIND_LONG_DOUBLE) {
    return is_complex ? AG_TARGET_SCALAR_LONG_DOUBLE_COMPLEX
                      : AG_TARGET_SCALAR_LONG_DOUBLE;
  }
  if (type && type->floating_kind == PSX_FLOATING_KIND_FLOAT)
    return is_complex ? AG_TARGET_SCALAR_FLOAT_COMPLEX
                      : AG_TARGET_SCALAR_FLOAT;
  return is_complex ? AG_TARGET_SCALAR_DOUBLE_COMPLEX
                    : AG_TARGET_SCALAR_DOUBLE;
}

static int layout_scalar(const psx_type_shape_t *type,
                         const ag_data_layout_t *data_layout,
                         psx_type_layout_t *out) {
  if (type->kind == PSX_TYPE_INTEGER &&
      type->integer_kind != PSX_INTEGER_KIND_CHAR &&
      type->integer_kind != PSX_INTEGER_KIND_SHORT &&
      type->integer_kind != PSX_INTEGER_KIND_INT &&
      type->integer_kind != PSX_INTEGER_KIND_LONG &&
      type->integer_kind != PSX_INTEGER_KIND_LONG_LONG &&
      type->integer_kind != PSX_INTEGER_KIND_ENUM) {
    return 1;
  }
  ag_target_scalar_kind_t kind;
  if (type->kind == PSX_TYPE_BOOL) {
    kind = AG_TARGET_SCALAR_CHAR;
  } else if (type->kind == PSX_TYPE_INTEGER) {
    kind = integer_target_kind(type);
  } else {
    kind = floating_target_kind(type);
  }
  out->size = ag_data_layout_scalar_size(data_layout, kind);
  out->alignment = ag_data_layout_scalar_alignment(data_layout, kind);
  out->is_complete = 1;
  return 1;
}

static int layout_non_array(const psx_type_shape_t *type,
                            const ag_data_layout_t *data_layout,
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
      out->size = ag_data_layout_pointer_size(data_layout);
      out->alignment = ag_data_layout_pointer_alignment(data_layout);
      out->is_complete = 1;
      return 1;
    case PSX_TYPE_ARRAY:
      return 0;
    case PSX_TYPE_BOOL:
    case PSX_TYPE_INTEGER:
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_COMPLEX:
      return layout_scalar(type, data_layout, out);
    case PSX_TYPE_STRUCT:
    case PSX_TYPE_UNION:
      return 1;
    default:
      return 0;
  }
}

static int
layout_non_array_with_records(const psx_type_shape_t *type,
                              const psx_record_layout_table_t *record_layouts,
                              const ag_data_layout_t *data_layout,
                              psx_type_layout_t *out) {
  if (!type || !out) return 0;
  if (type->kind != PSX_TYPE_STRUCT && type->kind != PSX_TYPE_UNION)
    return layout_non_array(type, data_layout, out);
  memset(out, 0, sizeof(*out));
  out->alignment = 1;
  if (type->record_id == PSX_RECORD_ID_INVALID) return 1;
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      record_layouts, type->record_id, data_layout);
  if (!layout) return 1;
  out->size = layout->size;
  out->alignment = layout->alignment;
  out->is_complete = 1;
  return 1;
}

static int complete_array_layout(
    const psx_type_shape_t *array_type, const psx_type_layout_t *element,
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

static int layout_of_id_recursive(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts, psx_type_id_t type_id,
    const ag_data_layout_t *data_layout, psx_type_layout_t *out) {
  psx_type_shape_t type = {0};
  if (!out || !psx_semantic_type_table_describe(types, type_id, &type))
    return 0;
  if (type.kind != PSX_TYPE_ARRAY)
    return layout_non_array_with_records(&type, record_layouts, data_layout,
                                         out);
  psx_type_id_t element_type_id = psx_semantic_type_table_base(
      types, type_id).type_id;
  psx_type_layout_t element = {0};
  if (!layout_of_id_recursive(types, record_layouts, element_type_id,
                              data_layout, &element))
    return 0;
  return complete_array_layout(&type, &element, out);
}

int psx_type_layout_of(const psx_semantic_type_table_t *types,
                       const psx_record_layout_table_t *record_layouts,
                       psx_type_id_t type_id,
                       const ag_data_layout_t *data_layout,
                       psx_type_layout_t *out) {
  if (!types || !record_layouts || !ag_data_layout_is_valid(data_layout) ||
      !out)
    return 0;
  return layout_of_id_recursive(types, record_layouts, type_id, data_layout,
                                out);
}

int psx_type_layout_sizeof(const psx_semantic_type_table_t *types,
                           const psx_record_layout_table_t *record_layouts,
                           psx_type_id_t type_id,
                           const ag_data_layout_t *data_layout) {
  psx_type_layout_t layout = {0};
  return psx_type_layout_of(types, record_layouts, type_id, data_layout,
                            &layout) &&
                 layout.is_complete
             ? layout.size
             : 0;
}

int psx_type_layout_alignof(const psx_semantic_type_table_t *types,
                            const psx_record_layout_table_t *record_layouts,
                            psx_type_id_t type_id,
                            const ag_data_layout_t *data_layout) {
  psx_type_layout_t layout = {0};
  return psx_type_layout_of(types, record_layouts, type_id, data_layout,
                            &layout)
             ? layout.alignment
             : 0;
}
