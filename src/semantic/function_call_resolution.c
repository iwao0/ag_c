#include "function_call_resolution.h"

#include <string.h>

static const psx_type_t *callable_function_type(const psx_type_t *type) {
  if (!type) return NULL;
  if (type->kind == PSX_TYPE_FUNCTION) return type;
  if (type->kind == PSX_TYPE_POINTER && type->base &&
      type->base->kind == PSX_TYPE_FUNCTION)
    return type->base;
  return NULL;
}

void psx_resolve_function_call_type(
    const psx_type_t *bound_function_type,
    const psx_type_t *callee_type, int is_implicit_declaration,
    psx_function_call_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_FUNCTION_CALL_RESOLUTION_NOT_CALLABLE;

  const psx_type_t *function = callable_function_type(callee_type);
  if (!function) function = callable_function_type(bound_function_type);
  if (function && function->base) {
    resolution->status = PSX_FUNCTION_CALL_RESOLUTION_OK;
    resolution->function_type = function;
    resolution->result_type = ps_type_clone(function->base);
    return;
  }
  if (is_implicit_declaration) {
    resolution->status = PSX_FUNCTION_CALL_RESOLUTION_OK;
    resolution->result_type = ps_type_new_integer(TK_INT, 4, 0);
  }
}

psx_type_t *psx_resolve_function_reference_type(
    const psx_type_t *function_type) {
  if (!function_type || function_type->kind != PSX_TYPE_FUNCTION)
    return NULL;
  psx_type_t *type = ps_type_new_pointer(
      ps_type_clone(function_type), 0);
  ps_type_get_funcptr_signature(function_type, &type->funcptr_sig);
  return type;
}
