#include "local_object_lowering.h"

#include "local_storage.h"
#include "../parser/local_registry.h"
#include "../semantic/local_declaration_plan.h"
#include <string.h>

int lower_complete_local_object(
    const psx_local_object_request_t *request,
    psx_local_object_result_t *result) {
  if (!request || !result || !request->name || request->name_len <= 0 ||
      !request->type) return 0;
  memset(result, 0, sizeof(*result));

  psx_complete_array_storage_plan_t array_plan = {0};
  psx_complete_object_storage_plan_t object_plan = {0};
  if (psx_plan_complete_array_storage(request->type, &array_plan)) {
    result->storage_size = array_plan.storage_size;
    result->element_size = array_plan.scalar_element_size;
    result->alignment = array_plan.alignment;
    result->is_array = 1;
  } else if (psx_plan_complete_object_storage(
                 request->type, &object_plan)) {
    result->storage_size = object_plan.storage_size;
    result->element_size = object_plan.element_size;
    result->alignment = object_plan.alignment;
  } else {
    return 0;
  }
  if (request->requested_alignment > 0)
    result->alignment = request->requested_alignment;

  int offset = local_storage_allocate(
      result->storage_size, result->alignment);
  result->var = ps_local_registry_create_storage_object(
      request->name, request->name_len, offset, result->storage_size,
      result->element_size, result->is_array, result->alignment);
  if (!result->var) return 0;
  ps_local_registry_set_decl_type(result->var, request->type);
  result->type_attached = 1;
  return 1;
}

int declare_incomplete_local_object(
    const psx_local_object_request_t *request,
    psx_local_object_result_t *result) {
  if (!request || !result || !request->name || request->name_len <= 0 ||
      !request->type || request->type->kind != PSX_TYPE_ARRAY ||
      request->type->array_len > 0 || request->type->is_vla)
    return 0;
  memset(result, 0, sizeof(*result));
  int element_size = ps_type_sizeof(request->type->base);
  if (element_size <= 0) element_size = 1;
  result->var = ps_local_registry_create_storage_object(
      request->name, request->name_len, 0, 0,
      element_size, 1, request->requested_alignment);
  if (!result->var) return 0;
  ps_local_registry_set_decl_type(result->var, request->type);
  result->element_size = element_size;
  result->is_array = 1;
  result->type_attached = 1;
  return 1;
}

int complete_declared_local_object(
    lvar_t *var, const psx_local_object_request_t *request,
    psx_local_object_result_t *result) {
  if (!var || !request || !result || !request->type) return 0;
  memset(result, 0, sizeof(*result));
  psx_complete_array_storage_plan_t plan = {0};
  if (!psx_plan_complete_array_storage(request->type, &plan)) return 0;
  result->storage_size = plan.storage_size;
  result->element_size = plan.scalar_element_size;
  result->alignment = request->requested_alignment > 0
                          ? request->requested_alignment : plan.alignment;
  result->is_array = 1;
  int offset = local_storage_allocate(
      result->storage_size, result->alignment);
  ps_local_registry_update_storage_object(
      var, offset, result->storage_size, result->element_size,
      result->alignment);
  ps_local_registry_set_decl_type(var, request->type);
  result->var = var;
  result->type_attached = 1;
  return 1;
}
