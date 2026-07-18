#include "local_object_lowering.h"

#include "local_storage.h"
#include "runtime_context.h"
#include "../parser/local_registry.h"
#include "../semantic/local_declaration_plan.h"

lvar_t *lower_complete_local_object(
    const psx_local_object_request_t *request) {
  if (!request || !request->name || request->name_len <= 0 ||
      !request->type || !request->local_registry ||
      !request->lowering_context) return NULL;

  psx_local_storage_plan_t plan = {0};
  if (!psx_plan_local_storage_for_type_id(
          ps_lowering_semantic_types(request->lowering_context),
          ps_lowering_record_layouts(request->lowering_context),
          ps_lowering_type_id(request->lowering_context, request->type),
          ps_lowering_target(request->lowering_context),
          &plan)) return NULL;
  int alignment = plan.alignment;
  if (request->requested_alignment > 0)
    alignment = request->requested_alignment;

  int offset = local_storage_allocate(
      request->lowering_context, plan.storage_size, alignment);
  return ps_local_registry_create_storage_object_in(
      request->local_registry,
      request->name, request->name_len, offset, plan.storage_size,
      alignment, request->type, request->diag_tok);
}

lvar_t *lower_complete_internal_local_object(
    const psx_local_object_request_t *request) {
  if (!request || !request->name || request->name_len <= 0 ||
      !request->type || !request->local_registry ||
      !request->lowering_context)
    return NULL;

  psx_local_storage_plan_t plan = {0};
  if (!psx_plan_local_storage_for_type_id(
          ps_lowering_semantic_types(request->lowering_context),
          ps_lowering_record_layouts(request->lowering_context),
          ps_lowering_type_id(request->lowering_context, request->type),
          ps_lowering_target(request->lowering_context), &plan))
    return NULL;
  int alignment = request->requested_alignment > 0
                      ? request->requested_alignment
                      : plan.alignment;
  int offset = local_storage_allocate(
      request->lowering_context, plan.storage_size, alignment);
  return ps_local_registry_create_internal_storage_object_in(
      request->local_registry, request->name, request->name_len,
      offset, plan.storage_size, alignment, request->type);
}

lvar_t *declare_incomplete_local_object(
    const psx_local_object_request_t *request) {
  if (!request || !request->name || request->name_len <= 0 ||
      !request->type || !request->local_registry ||
      request->type->kind != PSX_TYPE_ARRAY ||
      request->type->array_len > 0 || request->type->is_vla)
    return NULL;
  return ps_local_registry_create_storage_object_in(
      request->local_registry,
      request->name, request->name_len, 0, 0,
      request->requested_alignment, request->type,
      request->diag_tok);
}

int complete_declared_local_object(
    lvar_t *var, const psx_local_object_request_t *request) {
  if (!var || !request || !request->type || !request->local_registry ||
      !request->lowering_context ||
      request->type->kind != PSX_TYPE_ARRAY) return 0;
  psx_local_storage_plan_t plan = {0};
  if (!psx_plan_local_storage_for_type_id(
          ps_lowering_semantic_types(request->lowering_context),
          ps_lowering_record_layouts(request->lowering_context),
          ps_lowering_type_id(request->lowering_context, request->type),
          ps_lowering_target(request->lowering_context),
          &plan)) return 0;
  int alignment = request->requested_alignment > 0
                      ? request->requested_alignment : plan.alignment;
  int offset = local_storage_allocate(
      request->lowering_context, plan.storage_size, alignment);
  if (!ps_local_registry_complete_array_type(
          request->local_registry, var, request->type)) return 0;
  ps_local_registry_update_storage_object_in(
      request->local_registry, var, offset, plan.storage_size, alignment);
  return 1;
}
