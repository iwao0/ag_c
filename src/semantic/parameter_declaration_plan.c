#include "parameter_declaration_plan.h"
#include "../type_layout.h"

#include <string.h>

int psx_plan_parameter_storage(
    const psx_type_t *type, psx_parameter_storage_plan_t *plan) {
  ag_target_info_t target = ag_target_info_host();
  return psx_plan_parameter_storage_for_target(type, &target, plan);
}

int psx_plan_parameter_storage_for_target(
    const psx_type_t *type, const ag_target_info_t *target,
    psx_parameter_storage_plan_t *plan) {
  if (!type || !plan) return 0;
  memset(plan, 0, sizeof(*plan));

  if (type->kind == PSX_TYPE_POINTER) {
    plan->kind = PSX_PARAMETER_STORAGE_POINTER;
    plan->storage_size = ag_target_info_pointer_size(target);
    plan->alignment = plan->storage_size;
    return 1;
  }

  int size = ps_type_sizeof_for_target(type, target);
  if (size <= 0) return 0;
  if (ps_type_is_tag_aggregate(type)) {
    if (size > 16) {
      plan->kind = PSX_PARAMETER_STORAGE_AGGREGATE_BYREF;
      plan->storage_size = ag_target_info_pointer_size(target);
      plan->alignment = plan->storage_size;
      plan->is_byref = 1;
    } else {
      plan->kind = PSX_PARAMETER_STORAGE_AGGREGATE_VALUE;
      plan->storage_size = size;
      plan->alignment = ps_type_alignof_for_target(type, target);
    }
    return 1;
  }
  if (type->kind == PSX_TYPE_COMPLEX) {
    plan->kind = PSX_PARAMETER_STORAGE_COMPLEX;
    plan->storage_size = size;
    plan->alignment = ps_type_alignof_for_target(type, target);
    return 1;
  }
  if (!ps_type_is_scalar(type)) return 0;
  plan->kind = PSX_PARAMETER_STORAGE_SCALAR;
  plan->storage_size = size;
  return 1;
}
