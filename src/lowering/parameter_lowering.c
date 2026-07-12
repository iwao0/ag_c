#include "parameter_lowering.h"

#include "local_storage.h"
#include "vla_lowering.h"
#include "../parser/decl.h"
#include "../parser/local_registry.h"
#include "../semantic/local_type_state.h"
#include <string.h>

static int lower_parameter_with_plan(
    char *name, int name_len, const psx_type_t *type,
    const psx_parameter_storage_plan_t *storage,
    psx_parameter_lowering_result_t *result) {
  if (!name || name_len <= 0 || !type || !storage || !result) return 0;
  memset(result, 0, sizeof(*result));
  result->storage = *storage;

  int offset = local_storage_allocate(
      result->storage.storage_size, result->storage.alignment);
  result->var = ps_local_registry_create_storage_object(
      name, name_len, offset,
      result->storage.storage_size, result->storage.element_size,
      0, result->storage.alignment);
  if (!result->var) return 0;
  result->var->is_param = 1;
  result->var->is_byref_param = result->storage.is_byref ? 1 : 0;
  psx_decl_set_lvar_decl_type(result->var, type);
  result->type_attached = 1;
  return 1;
}

int lower_parameter_declaration(
    const psx_parameter_lowering_request_t *request,
    psx_parameter_lowering_result_t *result) {
  if (!request || !result || !request->name || request->name_len <= 0 ||
      !request->type) return 0;
  psx_parameter_storage_plan_t storage;
  if (!psx_plan_parameter_storage(request->type, &storage)) return 0;
  return lower_parameter_with_plan(
      request->name, request->name_len, request->type, &storage, result);
}

int lower_resolved_parameter_declaration(
    const psx_resolved_parameter_lowering_request_t *request,
    psx_parameter_lowering_result_t *result) {
  if (!request || !result || !request->name || request->name_len <= 0 ||
      !request->resolution || !request->resolution->type) return 0;
  const psx_parameter_declaration_resolution_t *resolution =
      request->resolution;
  if (resolution->lowering_kind == PSX_PARAMETER_LOWER_NORMAL) {
    return lower_parameter_with_plan(
        request->name, request->name_len, resolution->type,
        &resolution->storage, result);
  }

  psx_parameter_vla_lowering_request_t vla = {
      .name = request->name,
      .name_len = request->name_len,
      .element_size = resolution->element_size,
      .inner_dimension_count = resolution->inner_dimension_count,
      .type = resolution->type,
      .diag_tok = request->diag_tok,
  };
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
  memset(result, 0, sizeof(*result));
  result->var = lowered.var;
  result->storage = resolution->storage;
  result->type_attached = lowered.type_attached;
  return result->var != NULL;
}
