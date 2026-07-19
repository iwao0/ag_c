#include "parameter_declaration_plan.h"
#include "../type_layout.h"

#include <string.h>

int psx_plan_parameter_storage_for_type_id(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id,
    const ag_target_info_t *target,
    psx_parameter_storage_plan_t *plan) {
  const psx_type_t *type = psx_semantic_type_table_lookup(types, type_id);
  if (!type || !plan) return 0;
  memset(plan, 0, sizeof(*plan));

  if (type->kind == PSX_TYPE_POINTER) {
    plan->kind = PSX_PARAMETER_STORAGE_POINTER;
    plan->storage_size = ag_target_info_pointer_size(target);
    plan->alignment = ag_target_info_pointer_alignment(target);
    return 1;
  }

  int size = ps_type_sizeof_id(
      types, record_layouts, type_id, target);
  if (size <= 0) return 0;
  if (ps_type_is_tag_aggregate(type)) {
    if (size > 16) {
      plan->kind = PSX_PARAMETER_STORAGE_AGGREGATE_BYREF;
      plan->storage_size = ag_target_info_pointer_size(target);
      plan->alignment = ag_target_info_pointer_alignment(target);
      plan->is_byref = 1;
    } else {
      plan->kind = PSX_PARAMETER_STORAGE_AGGREGATE_VALUE;
      plan->storage_size = size;
      plan->alignment =
          ps_type_alignof_id(
              types, record_layouts, type_id, target);
    }
    return 1;
  }
  if (type->kind == PSX_TYPE_COMPLEX) {
    plan->kind = PSX_PARAMETER_STORAGE_COMPLEX;
    plan->storage_size = size;
    plan->alignment =
        ps_type_alignof_id(
            types, record_layouts, type_id, target);
    return 1;
  }
  if (!ps_type_is_scalar(type)) return 0;
  plan->kind = PSX_PARAMETER_STORAGE_SCALAR;
  plan->storage_size = size;
  return 1;
}
