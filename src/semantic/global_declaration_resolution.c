#include "global_declaration_resolution.h"

#include "../parser/global_registry.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"

#include <string.h>

static int global_types_compatible(
    const psx_type_t *existing, const psx_type_t *incoming) {
  if (ps_type_shape_matches(existing, incoming)) return 1;
  if (!existing || !incoming || existing->kind != PSX_TYPE_ARRAY ||
      incoming->kind != PSX_TYPE_ARRAY ||
      (existing->array_len > 0 && incoming->array_len > 0)) {
    return 0;
  }
  return ps_type_shape_matches(existing->base, incoming->base);
}

static int is_incomplete_object_type(const psx_type_t *type) {
  if (!type) return 0;
  const psx_type_t *value = ps_type_array_leaf_type(type);
  return value && value->kind != PSX_TYPE_POINTER &&
         ps_type_is_tag_aggregate(value) && ps_type_sizeof(value) <= 0;
}

void psx_resolve_global_declaration(
    const psx_global_declaration_resolution_request_t *request,
    psx_global_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_GLOBAL_DECLARATION_INVALID;
  if (!request || !request->name || request->name_len <= 0 ||
      !request->type) return;
  psx_semantic_context_t *semantic_context = request->semantic_context
      ? request->semantic_context : ps_ctx_active();
  psx_global_registry_t *global_registry = request->global_registry
      ? request->global_registry : ps_global_registry_active();

  if (is_incomplete_object_type(request->type) ||
      (request->type->kind == PSX_TYPE_ARRAY &&
       request->type->array_len <= 0 && !request->is_extern_decl &&
       !request->has_initializer)) {
    resolution->status = PSX_GLOBAL_DECLARATION_INCOMPLETE_OBJECT;
    return;
  }
  int incoming_is_incomplete_array =
      request->type->kind == PSX_TYPE_ARRAY &&
      request->type->array_len <= 0;
  if (!incoming_is_incomplete_array && ps_type_sizeof(request->type) <= 0) {
    return;
  }
  if (ps_ctx_has_function_name_in(
          semantic_context, request->name, request->name_len)) {
    resolution->status = PSX_GLOBAL_DECLARATION_FUNCTION_NAME_CONFLICT;
    return;
  }
  psx_typedef_info_t typedef_info;
  if (ps_ctx_find_typedef_name_in(
          semantic_context, request->name, request->name_len,
          &typedef_info)) {
    resolution->status = PSX_GLOBAL_DECLARATION_TYPEDEF_NAME_CONFLICT;
    return;
  }
  long long enum_value = 0;
  if (ps_ctx_find_enum_const_in(
          semantic_context, request->name, request->name_len,
          &enum_value)) {
    resolution->status = PSX_GLOBAL_DECLARATION_ENUM_NAME_CONFLICT;
    return;
  }

  resolution->existing = ps_find_global_var_in(
      global_registry, request->name, request->name_len);
  if (resolution->existing) {
    const psx_type_t *existing_type =
        ps_gvar_get_decl_type(resolution->existing);
    if (!global_types_compatible(existing_type, request->type)) {
      resolution->status = PSX_GLOBAL_DECLARATION_TYPE_CONFLICT;
      return;
    }
    int existing_is_incomplete =
        existing_type && existing_type->kind == PSX_TYPE_ARRAY &&
        existing_type->array_len <= 0;
    resolution->complete_existing_array =
        existing_is_incomplete && !incoming_is_incomplete_array;
    resolution->clear_existing_extern =
        resolution->existing->is_extern_decl && !request->is_extern_decl;
  }
  resolution->status = PSX_GLOBAL_DECLARATION_OK;
}
