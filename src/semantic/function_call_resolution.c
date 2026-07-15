#include "function_call_resolution.h"

#include "../parser/semantic_ctx.h"
#include "../parser/type_builder.h"

#include <string.h>

void psx_resolve_function_call_type(
    const psx_type_t *bound_function_type,
    const psx_type_t *callee_type, int is_implicit_declaration,
    psx_function_call_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_FUNCTION_CALL_RESOLUTION_NOT_CALLABLE;

  const psx_type_t *function = ps_type_callable_function(callee_type);
  if (!function)
    function = ps_type_callable_function(bound_function_type);
  if (function && function->base) {
    resolution->status = PSX_FUNCTION_CALL_RESOLUTION_OK;
    resolution->function_type = function;
    return;
  }
  if (is_implicit_declaration)
    resolution->status = PSX_FUNCTION_CALL_RESOLUTION_OK;
}

const psx_type_t *psx_resolve_function_reference_type(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *function_type) {
  if (!function_type || function_type->kind != PSX_TYPE_FUNCTION)
    return NULL;
  arena_context_t *arena_context = ps_ctx_arena(semantic_context);
  return ps_type_new_pointer_in(
      arena_context, ps_type_clone_in(arena_context, function_type));
}
