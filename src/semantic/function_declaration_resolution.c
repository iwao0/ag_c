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
      !request->return_type || request->parameter_count < 0) {
    return;
  }
  if (ps_find_global_var(request->name, request->name_len)) {
    resolution->status = PSX_FUNCTION_DECLARATION_OBJECT_NAME_CONFLICT;
    return;
  }
  if (!psx_plan_function_declaration(
          &(psx_function_declaration_request_t){
              .return_type = request->return_type,
              .parameter_types = request->parameter_types,
              .parameter_count = request->parameter_count,
              .is_variadic = request->is_variadic,
          },
          &resolution->declaration_plan)) {
    return;
  }
  if (!psx_ctx_register_function_type(
          request->name, request->name_len,
          resolution->declaration_plan.function_type)) {
    resolution->status = PSX_FUNCTION_DECLARATION_TYPE_CONFLICT;
    return;
  }
  if (request->is_definition &&
      !psx_ctx_track_function_defined(request->name, request->name_len)) {
    resolution->status = PSX_FUNCTION_DECLARATION_DUPLICATE_DEFINITION;
    return;
  }
  resolution->status = PSX_FUNCTION_DECLARATION_OK;
}
