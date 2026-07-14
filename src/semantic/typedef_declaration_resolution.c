#include "typedef_declaration_resolution.h"

#include "../parser/decl.h"
#include "../parser/function_public.h"
#include "../parser/gvar_public.h"
#include "../parser/local_registry.h"
#include "../parser/semantic_ctx.h"
#include "../parser/global_registry.h"

#include <string.h>

void psx_resolve_typedef_declaration(
    const psx_typedef_declaration_resolution_request_t *request,
    psx_typedef_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_TYPEDEF_DECLARATION_INVALID;
  if (!request || !request->semantic_context || !request->global_registry ||
      !request->local_registry || !request->name || request->name_len <= 0 ||
      !request->type) {
    return;
  }
  psx_semantic_context_t *semantic_context = request->semantic_context;
  psx_local_registry_t *local_registry = request->local_registry;
  psx_global_registry_t *global_registry = request->global_registry;

  int scope_depth = ps_ctx_current_tag_scope_depth_in(semantic_context);
  if (ps_ctx_has_enum_const_in_current_scope_in(
          semantic_context, request->name, request->name_len)) {
    resolution->status = PSX_TYPEDEF_DECLARATION_ENUM_NAME_CONFLICT;
    return;
  }
  if (scope_depth == 0) {
    if (ps_find_global_var_in(
            global_registry, request->name, request->name_len)) {
      resolution->status = PSX_TYPEDEF_DECLARATION_OBJECT_NAME_CONFLICT;
      return;
    }
    if (ps_ctx_has_function_name_in(
            semantic_context, request->name, request->name_len)) {
      resolution->status = PSX_TYPEDEF_DECLARATION_FUNCTION_NAME_CONFLICT;
      return;
    }
  } else {
    lvar_t *local = ps_decl_find_lvar_in(
        local_registry, request->name, request->name_len);
    if (local && ps_lvar_registry_view(local).scope_seq ==
                     ps_local_registry_current_scope_seq_in(local_registry)) {
      resolution->status = PSX_TYPEDEF_DECLARATION_OBJECT_NAME_CONFLICT;
      return;
    }
  }

  const psx_typedef_info_t info = {.decl_type = request->type};
  if (!ps_ctx_register_typedef_name_in_contexts(
          semantic_context, local_registry,
          request->name, request->name_len, &info,
          &resolution->created, &resolution->redeclared)) {
    resolution->status = PSX_TYPEDEF_DECLARATION_TYPE_CONFLICT;
    return;
  }
  resolution->scope_depth = scope_depth;
  resolution->status = PSX_TYPEDEF_DECLARATION_OK;
}
