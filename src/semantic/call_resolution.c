#include "call_resolution.h"

#include <string.h>

static psx_qual_type_t invalid_qual_type(void) {
  return (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
}

void psx_resolve_call_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t callee_qual_type,
    int argument_count,
    psx_call_types_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_CALL_TYPES_NOT_CALLABLE;
  resolution->function_qual_type = invalid_qual_type();
  resolution->return_qual_type = invalid_qual_type();
  if (!semantic_context || argument_count < 0 ||
      callee_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return;

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_qual_type_t function_type =
      psx_semantic_type_table_callable_function(
          types, callee_qual_type);
  const psx_type_t *function = psx_semantic_type_table_lookup(
      types, function_type.type_id);
  if (!function || function->kind != PSX_TYPE_FUNCTION)
    return;

  resolution->function_qual_type = function_type;
  resolution->return_qual_type = psx_semantic_type_table_base(
      types, function_type.type_id);
  resolution->parameter_count = function->param_count;
  resolution->is_variadic =
      function->is_variadic_function ? 1 : 0;
  if (resolution->return_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return;

  if ((function->has_function_prototype &&
       function->param_count == 0 && argument_count > 0) ||
      (function->is_variadic_function &&
       argument_count < function->param_count) ||
      (function->has_function_prototype &&
       !function->is_variadic_function &&
       function->param_count > 0 &&
       argument_count != function->param_count)) {
    resolution->status = PSX_CALL_TYPES_ARGUMENT_COUNT_MISMATCH;
    return;
  }
  resolution->status = PSX_CALL_TYPES_OK;
}
