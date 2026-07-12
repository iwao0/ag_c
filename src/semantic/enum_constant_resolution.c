#include "enum_constant_resolution.h"

#include "../parser/decl.h"
#include "../parser/function_public.h"
#include "../parser/gvar_public.h"
#include "../parser/local_registry.h"
#include "../parser/semantic_ctx.h"

#include <string.h>

void psx_resolve_enum_constant(
    const psx_enum_constant_resolution_request_t *request,
    psx_enum_constant_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_ENUM_CONSTANT_INVALID;
  if (!request || !request->name || request->name_len <= 0) return;

  int scope_depth = psx_ctx_current_tag_scope_depth();
  if (psx_ctx_has_typedef_in_current_scope(
          request->name, request->name_len)) {
    resolution->status = PSX_ENUM_CONSTANT_TYPEDEF_NAME_CONFLICT;
    return;
  }
  if (scope_depth == 0) {
    if (ps_find_global_var(request->name, request->name_len)) {
      resolution->status = PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT;
      return;
    }
    if (ps_ctx_has_function_name(request->name, request->name_len)) {
      resolution->status = PSX_ENUM_CONSTANT_FUNCTION_NAME_CONFLICT;
      return;
    }
  } else {
    lvar_t *local = psx_decl_find_lvar(request->name, request->name_len);
    if (local && local->scope_seq ==
                     psx_local_registry_current_scope_seq()) {
      resolution->status = PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT;
      return;
    }
  }

  if (!psx_ctx_register_enum_const(
          request->name, request->name_len, request->value,
          &resolution->created)) {
    resolution->status = PSX_ENUM_CONSTANT_DUPLICATE;
    return;
  }
  resolution->scope_depth = scope_depth;
  resolution->status = PSX_ENUM_CONSTANT_OK;
}
