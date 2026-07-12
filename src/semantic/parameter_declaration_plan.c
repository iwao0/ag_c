#include "parameter_declaration_plan.h"

#include <string.h>

int psx_plan_parameter_storage(
    const psx_type_t *type, psx_parameter_storage_plan_t *plan) {
  if (!type || !plan) return 0;
  memset(plan, 0, sizeof(*plan));

  if (type->kind == PSX_TYPE_POINTER) {
    plan->kind = PSX_PARAMETER_STORAGE_POINTER;
    plan->storage_size = 8;
    plan->element_size = ps_type_deref_size(type);
    if (plan->element_size <= 0) plan->element_size = 8;
    return 1;
  }

  int size = ps_type_sizeof(type);
  if (size <= 0) return 0;
  if (ps_type_is_tag_aggregate(type)) {
    plan->element_size = size;
    if (size > 16) {
      plan->kind = PSX_PARAMETER_STORAGE_AGGREGATE_BYREF;
      plan->storage_size = 8;
      plan->is_byref = 1;
    } else {
      plan->kind = PSX_PARAMETER_STORAGE_AGGREGATE_VALUE;
      plan->storage_size = size;
      plan->alignment = 8;
    }
    return 1;
  }
  if (type->kind == PSX_TYPE_COMPLEX) {
    plan->kind = PSX_PARAMETER_STORAGE_COMPLEX;
    plan->storage_size = size;
    plan->element_size = size;
    plan->alignment = 8;
    return 1;
  }
  if (!ps_type_is_scalar(type)) return 0;
  plan->kind = PSX_PARAMETER_STORAGE_SCALAR;
  plan->storage_size = size;
  plan->element_size = size;
  return 1;
}
