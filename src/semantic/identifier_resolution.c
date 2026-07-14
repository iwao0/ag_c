#include "identifier_resolution.h"

#include "../parser/decl.h"
#include "../parser/semantic_ctx.h"

#include <string.h>

global_var_t *psx_resolve_global_object_symbol(
    char *name, int name_len) {
  if (!name || name_len <= 0) return NULL;
  return ps_find_global_var(name, name_len);
}

void psx_resolve_identifier(
    const psx_identifier_resolution_request_t *request,
    psx_identifier_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  if (!request || !request->name || request->name_len <= 0) return;
  psx_semantic_context_t *semantic_context = request->semantic_context
      ? request->semantic_context : ps_ctx_active();
  psx_local_registry_t *local_registry = request->local_registry
      ? request->local_registry : ps_local_registry_active();

  resolution->local = request->has_local_lookup_point
      ? ps_local_registry_find_visible_in(
            local_registry,
            request->name, request->name_len,
            request->local_lookup_point)
      : ps_decl_find_lvar_in(
            local_registry, request->name, request->name_len);
  if (resolution->local) {
    resolution->kind = PSX_IDENTIFIER_LOCAL;
    return;
  }
  int has_enum = request->has_local_lookup_point
      ? ps_ctx_find_enum_const_at_in_contexts(
            semantic_context, local_registry,
            request->name, request->name_len,
            request->local_lookup_point, &resolution->enum_value)
      : ps_ctx_find_enum_const_in(
            semantic_context,
            request->name, request->name_len,
            &resolution->enum_value);
  if (has_enum) {
    resolution->kind = PSX_IDENTIFIER_ENUM_CONSTANT;
    return;
  }

  resolution->global = psx_resolve_global_object_symbol(
      request->name, request->name_len);
  const psx_function_symbol_t *function =
      ps_ctx_find_function_symbol_in(
          semantic_context,
          request->name, request->name_len);
  if (request->is_call) {
    if (resolution->global) {
      resolution->kind = PSX_IDENTIFIER_GLOBAL_OBJECT;
      return;
    }
    if (!function) {
      resolution->kind = PSX_IDENTIFIER_UNDECLARED_CALL;
      return;
    }
    resolution->kind = PSX_IDENTIFIER_FUNCTION;
    resolution->function = function;
    return;
  }

  if (function) {
    resolution->kind = PSX_IDENTIFIER_FUNCTION;
    resolution->function = function;
    return;
  }
  if (resolution->global) {
    resolution->kind = PSX_IDENTIFIER_GLOBAL_OBJECT;
    return;
  }
  resolution->kind = PSX_IDENTIFIER_UNDEFINED;
}
