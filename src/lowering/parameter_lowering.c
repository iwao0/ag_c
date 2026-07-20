#include "parameter_lowering.h"

#include "../parser/arena.h"
#include "../parser/decl.h"
#include "../parser/local_registry.h"
#include "../semantic/type_identity.h"
#include "abi_target_policy.h"
#include "local_storage.h"
#include "runtime_context.h"
#include "vla_lowering.h"

static int plan_parameter_storage(psx_lowering_context_t *lowering_context,
                                  psx_type_id_t type_id,
                                  psx_parameter_storage_plan_t *storage) {
  const ag_target_info_t *target = ps_lowering_target(lowering_context);
  return psx_plan_parameter_storage_for_type_id(
      ps_lowering_semantic_types(lowering_context),
      ps_lowering_record_layouts(lowering_context), type_id,
      ps_lowering_data_layout(lowering_context),
      ir_abi_target_policy_for(target), storage);
}

static lvar_t *lower_parameter_with_plan(
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    char *name, int name_len, psx_qual_type_t type,
    const psx_parameter_storage_plan_t *storage,
    token_t *diagnostic_token) {
  if (!local_registry || !lowering_context || !name || name_len <= 0 ||
      type.type_id == PSX_TYPE_ID_INVALID || !storage)
    return NULL;

  int offset = local_storage_allocate(
      lowering_context, storage->storage_size, storage->alignment);
  lvar_t *var = ps_local_registry_create_storage_object_qual_type_in(
      local_registry,
      name, name_len, offset,
      storage->storage_size, storage->alignment, type,
      diagnostic_token);
  if (!var) return NULL;
  ps_local_registry_mark_parameter(var, storage->is_byref);
  return var;
}

lvar_t *lower_resolved_parameter_declaration(
    const psx_resolved_parameter_lowering_request_t *request) {
  if (!request || !request->name || request->name_len <= 0 ||
      !request->resolution ||
      request->resolution->declaration_qual_type.type_id ==
          PSX_TYPE_ID_INVALID ||
      !request->local_registry || !request->lowering_context) return NULL;
  const psx_parameter_declaration_resolution_t *resolution =
      request->resolution;
  if (resolution->lowering_kind == PSX_PARAMETER_LOWER_NORMAL) {
    psx_parameter_storage_plan_t storage;
    if (!plan_parameter_storage(request->lowering_context,
                                resolution->declaration_qual_type.type_id,
                                &storage))
      return NULL;
    return lower_parameter_with_plan(
        request->local_registry, request->lowering_context, request->name,
        request->name_len, resolution->declaration_qual_type, &storage,
        request->diag_tok);
  }

  psx_parameter_vla_lowering_request_t vla = {
      .local_registry = request->local_registry,
      .lowering_context = request->lowering_context,
      .semantic_expressions = request->semantic_expressions,
      .name = request->name,
      .name_len = request->name_len,
      .inner_dimension_count = resolution->inner_dimension_count,
      .type = resolution->declaration_qual_type,
      .stride_storage_type = {
          resolution->runtime_stride_storage_type_id,
          PSX_TYPE_QUALIFIER_NONE,
      },
      .diag_tok = request->diag_tok,
  };
  if (resolution->inner_dimension_count > 0) {
    vla.inner_dimensions = arena_alloc_in(
        ps_lowering_arena(request->lowering_context),
        (size_t)resolution->inner_dimension_count *
        sizeof(*vla.inner_dimensions));
  }
  for (int i = 0; i < resolution->inner_dimension_count; i++) {
    vla.inner_dimensions[i].expression_id =
        resolution->inner_dimensions[i].expression_id;
    vla.inner_dimensions[i].constant_value =
        resolution->inner_dimensions[i].constant_value;
    vla.inner_dimensions[i].is_constant =
        resolution->inner_dimensions[i].is_constant;
  }
  psx_parameter_vla_lowering_result_t lowered =
      lower_parameter_vla_declaration(&vla);
  return lowered.var;
}
