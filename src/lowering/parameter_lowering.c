#include "parameter_lowering.h"

#include "local_storage.h"
#include "runtime_context.h"
#include "vla_lowering.h"
#include "../parser/arena.h"
#include "../parser/decl.h"
#include "../parser/local_registry.h"
#include "../semantic/type_identity.h"

static lvar_t *lower_parameter_with_plan(
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    char *name, int name_len, const psx_type_t *type,
    const psx_parameter_storage_plan_t *storage,
    token_t *diagnostic_token) {
  if (!local_registry || !lowering_context || !name || name_len <= 0 ||
      !type || !storage)
    return NULL;

  int offset = local_storage_allocate(
      lowering_context, storage->storage_size, storage->alignment);
  lvar_t *var = ps_local_registry_create_storage_object_in(
      local_registry,
      name, name_len, offset,
      storage->storage_size, storage->alignment, type,
      diagnostic_token);
  if (!var) return NULL;
  ps_local_registry_mark_parameter(var, storage->is_byref);
  return var;
}

lvar_t *lower_parameter_declaration(
    const psx_parameter_lowering_request_t *request) {
  if (!request || !request->name || request->name_len <= 0 ||
      !request->type || !request->local_registry ||
      !request->lowering_context) return NULL;
  psx_parameter_storage_plan_t storage;
  if (!psx_plan_parameter_storage_for_type_id(
          ps_lowering_semantic_types(request->lowering_context),
          ps_lowering_record_layouts(request->lowering_context),
          ps_lowering_type_id(request->lowering_context, request->type),
          ps_lowering_target(request->lowering_context),
          &storage)) return NULL;
  return lower_parameter_with_plan(
      request->local_registry, request->lowering_context,
      request->name, request->name_len, request->type, &storage,
      request->diag_tok);
}

lvar_t *lower_resolved_parameter_declaration(
    const psx_resolved_parameter_lowering_request_t *request) {
  if (!request || !request->name || request->name_len <= 0 ||
      !request->resolution || !request->resolution->type ||
      !request->local_registry || !request->lowering_context) return NULL;
  const psx_parameter_declaration_resolution_t *resolution =
      request->resolution;
  if (resolution->lowering_kind == PSX_PARAMETER_LOWER_NORMAL) {
    return lower_parameter_with_plan(
        request->local_registry, request->lowering_context,
        request->name, request->name_len, resolution->type,
        &resolution->storage, request->diag_tok);
  }

  psx_parameter_vla_lowering_request_t vla = {
      .local_registry = request->local_registry,
      .lowering_context = request->lowering_context,
      .name = request->name,
      .name_len = request->name_len,
      .inner_dimension_count = resolution->inner_dimension_count,
      .type = resolution->type,
      .stride_storage_type = psx_semantic_type_table_lookup(
          ps_lowering_semantic_types(request->lowering_context),
          resolution->runtime_stride_storage_type_id),
      .diag_tok = request->diag_tok,
  };
  if (resolution->inner_dimension_count > 0) {
    vla.inner_dimensions = arena_alloc_in(
        ps_lowering_arena(request->lowering_context),
        (size_t)resolution->inner_dimension_count *
        sizeof(*vla.inner_dimensions));
  }
  for (int i = 0; i < resolution->inner_dimension_count; i++) {
    vla.inner_dimensions[i].expression =
        request->inner_dimension_expressions
            ? request->inner_dimension_expressions[i]
            : NULL;
    vla.inner_dimensions[i].constant_value =
        resolution->inner_dimensions[i].constant_value;
    vla.inner_dimensions[i].is_constant =
        resolution->inner_dimensions[i].is_constant;
  }
  psx_parameter_vla_lowering_result_t lowered =
      lower_parameter_vla_declaration(&vla);
  return lowered.var;
}
