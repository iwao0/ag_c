#include "typedef_declaration_resolution.h"

#include "../parser/decl.h"
#include "../parser/function_public.h"
#include "../parser/gvar_public.h"
#include "../parser/local_registry.h"
#include "../parser/semantic_ctx.h"

#include <string.h>

void psx_resolve_typedef_declaration(
    const psx_typedef_declaration_resolution_request_t *request,
    psx_typedef_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_TYPEDEF_DECLARATION_INVALID;
  if (!request || !request->name || request->name_len <= 0 ||
      !request->type) return;

  int scope_depth = psx_ctx_current_tag_scope_depth();
  if (psx_ctx_has_enum_const_in_current_scope(
          request->name, request->name_len)) {
    resolution->status = PSX_TYPEDEF_DECLARATION_ENUM_NAME_CONFLICT;
    return;
  }
  if (scope_depth == 0) {
    if (ps_find_global_var(request->name, request->name_len)) {
      resolution->status = PSX_TYPEDEF_DECLARATION_OBJECT_NAME_CONFLICT;
      return;
    }
    if (ps_ctx_has_function_name(request->name, request->name_len)) {
      resolution->status = PSX_TYPEDEF_DECLARATION_FUNCTION_NAME_CONFLICT;
      return;
    }
  } else {
    lvar_t *local = psx_decl_find_lvar(request->name, request->name_len);
    if (local && local->scope_seq ==
                     psx_local_registry_current_scope_seq()) {
      resolution->status = PSX_TYPEDEF_DECLARATION_OBJECT_NAME_CONFLICT;
      return;
    }
  }

  psx_typedef_info_t info = {0};
  psx_ctx_typedef_set_decl_type(&info, (psx_type_t *)request->type);
  if (!psx_ctx_register_typedef_name(
          request->name, request->name_len, &info,
          &resolution->created, &resolution->redeclared)) {
    resolution->status = PSX_TYPEDEF_DECLARATION_TYPE_CONFLICT;
    return;
  }
  resolution->scope_depth = scope_depth;
  resolution->status = PSX_TYPEDEF_DECLARATION_OK;
}
