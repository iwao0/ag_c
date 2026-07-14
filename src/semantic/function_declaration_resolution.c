#include "function_declaration_resolution.h"

#include "../parser/gvar_public.h"
#include "../parser/semantic_ctx.h"

#include <string.h>

void psx_resolve_function_declaration(
    const psx_function_declaration_resolution_request_t *request,
    psx_function_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_FUNCTION_DECLARATION_INVALID;
  if (!request || !request->name || request->name_len <= 0 ||
      !request->function_type ||
      request->function_type->kind != PSX_TYPE_FUNCTION) {
    return;
  }
  if (ps_find_global_var(request->name, request->name_len)) {
    resolution->status = PSX_FUNCTION_DECLARATION_OBJECT_NAME_CONFLICT;
    return;
  }
  if (!request->function_type->base ||
      !ps_type_is_well_formed(request->function_type)) {
    return;
  }
  psx_semantic_context_t *semantic_context = request->semantic_context
      ? request->semantic_context : ps_ctx_active();
  resolution->function = ps_ctx_register_function_type_in(
      semantic_context, request->name, request->name_len,
      request->function_type);
  if (!resolution->function) {
    resolution->status = PSX_FUNCTION_DECLARATION_TYPE_CONFLICT;
    return;
  }
  if (request->is_definition &&
      !ps_ctx_track_function_defined_in(
          semantic_context, request->name, request->name_len)) {
    resolution->function = NULL;
    resolution->status = PSX_FUNCTION_DECLARATION_DUPLICATE_DEFINITION;
    return;
  }
  resolution->status = PSX_FUNCTION_DECLARATION_OK;
}
