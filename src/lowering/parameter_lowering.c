#include "parameter_lowering.h"

#include "local_storage.h"
#include "vla_lowering.h"
#include "../parser/arena.h"
#include "../parser/decl.h"
#include "../parser/local_registry.h"

static lvar_t *lower_parameter_with_plan(
    char *name, int name_len, const psx_type_t *type,
    const psx_parameter_storage_plan_t *storage) {
  if (!name || name_len <= 0 || !type || !storage) return NULL;

  int offset = local_storage_allocate(
      storage->storage_size, storage->alignment);
  lvar_t *var = ps_local_registry_create_storage_object(
      name, name_len, offset,
      storage->storage_size, storage->alignment, type);
  if (!var) return NULL;
  ps_local_registry_mark_parameter(var, storage->is_byref);
  return var;
}

lvar_t *lower_parameter_declaration(
    const psx_parameter_lowering_request_t *request) {
  if (!request || !request->name || request->name_len <= 0 ||
      !request->type) return NULL;
  psx_parameter_storage_plan_t storage;
  if (!psx_plan_parameter_storage(request->type, &storage)) return NULL;
  return lower_parameter_with_plan(
      request->name, request->name_len, request->type, &storage);
}

lvar_t *lower_resolved_parameter_declaration(
    const psx_resolved_parameter_lowering_request_t *request) {
  if (!request || !request->name || request->name_len <= 0 ||
      !request->resolution || !request->resolution->type) return NULL;
  const psx_parameter_declaration_resolution_t *resolution =
      request->resolution;
  if (resolution->lowering_kind == PSX_PARAMETER_LOWER_NORMAL) {
    return lower_parameter_with_plan(
        request->name, request->name_len, resolution->type,
        &resolution->storage);
  }

  psx_parameter_vla_lowering_request_t vla = {
      .name = request->name,
      .name_len = request->name_len,
      .inner_dimension_count = resolution->inner_dimension_count,
      .type = resolution->type,
      .diag_tok = request->diag_tok,
  };
  if (resolution->inner_dimension_count > 0) {
    vla.inner_dimensions = arena_alloc(
        (size_t)resolution->inner_dimension_count *
        sizeof(*vla.inner_dimensions));
  }
  for (int i = 0; i < resolution->inner_dimension_count; i++) {
    vla.inner_dimensions[i].constant =
        resolution->inner_dimensions[i].constant;
    vla.inner_dimensions[i].source_name =
        resolution->inner_dimensions[i].source_name;
    vla.inner_dimensions[i].source_name_len =
        resolution->inner_dimensions[i].source_name_len;
  }
  psx_parameter_vla_lowering_result_t lowered =
      lower_parameter_vla_declaration(&vla);
  return lowered.var;
}
