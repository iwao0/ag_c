#include "global_declaration_plan.h"

#include <string.h>

int psx_plan_global_object_storage(
    const psx_type_t *type, psx_global_storage_plan_t *plan) {
  if (!type || !plan) return 0;
  memset(plan, 0, sizeof(*plan));
  if (type->kind == PSX_TYPE_ARRAY && type->array_len <= 0) {
    plan->is_incomplete_array = 1;
    return 1;
  }
  plan->storage_size = ps_type_sizeof(type);
  return plan->storage_size > 0;
}
