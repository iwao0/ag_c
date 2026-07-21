#include "parameter_storage_plan.h"

#include "../target_info.h"
#include "../type_layout.h"

#include <string.h>

int psx_plan_parameter_storage_for_type_id(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_data_layout_t *data_layout,
    psx_parameter_storage_plan_t *plan) {
  psx_type_shape_t type = {0};
  if (!psx_semantic_type_table_describe(types, type_id, &type) ||
      !ag_data_layout_is_valid(data_layout) || !plan)
    return 0;
  memset(plan, 0, sizeof(*plan));

  if (type.kind == PSX_TYPE_POINTER) {
    plan->kind = PSX_PARAMETER_STORAGE_POINTER;
    plan->storage_size = ag_data_layout_pointer_size(data_layout);
    plan->alignment = ag_data_layout_pointer_alignment(data_layout);
    return 1;
  }

  int size = psx_type_layout_sizeof(
      types, record_layouts, type_id, data_layout);
  if (size <= 0) return 0;
  if (type.kind == PSX_TYPE_STRUCT || type.kind == PSX_TYPE_UNION) {
    plan->kind = PSX_PARAMETER_STORAGE_AGGREGATE_VALUE;
    plan->storage_size = size;
    plan->alignment = psx_type_layout_alignof(
        types, record_layouts, type_id, data_layout);
    return 1;
  }
  if (type.kind == PSX_TYPE_COMPLEX) {
    plan->kind = PSX_PARAMETER_STORAGE_COMPLEX;
    plan->storage_size = size;
    plan->alignment = psx_type_layout_alignof(
        types, record_layouts, type_id, data_layout);
    return 1;
  }
  if (type.kind != PSX_TYPE_BOOL && type.kind != PSX_TYPE_INTEGER &&
      type.kind != PSX_TYPE_FLOAT)
    return 0;
  plan->kind = PSX_PARAMETER_STORAGE_SCALAR;
  plan->storage_size = size;
  return 1;
}
