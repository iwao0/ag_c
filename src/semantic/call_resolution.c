#include "call_resolution.h"

#include <string.h>

#include "assignment_resolution.h"
#include "type_completeness.h"

static psx_qual_type_t invalid_qual_type(void) {
  return (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
}

void psx_resolve_call_qual_types_in(
    psx_semantic_context_t *semantic_context,
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
  psx_type_shape_t function = {0};
  if (!psx_semantic_type_table_describe(
          types, function_type.type_id, &function) ||
      function.kind != PSX_TYPE_FUNCTION)
    return;

  resolution->function_qual_type = function_type;
  resolution->return_qual_type = psx_semantic_type_table_base(
      types, function_type.type_id);
  resolution->parameter_count = function.parameter_count;
  resolution->is_variadic =
      function.is_variadic_function ? 1 : 0;
  if (resolution->return_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return;
  psx_type_shape_t return_shape = {0};
  if (!psx_semantic_type_table_describe(
          types, resolution->return_qual_type.type_id,
          &return_shape))
    return;
  if (return_shape.kind != PSX_TYPE_VOID &&
      !psx_semantic_type_is_complete_object_in(
          semantic_context,
          resolution->return_qual_type.type_id)) {
    resolution->status = PSX_CALL_TYPES_INCOMPLETE_RETURN;
    return;
  }

  if ((function.has_function_prototype &&
       function.parameter_count == 0 && argument_count > 0) ||
      (function.is_variadic_function &&
       argument_count < function.parameter_count) ||
      (function.has_function_prototype &&
       !function.is_variadic_function &&
       function.parameter_count > 0 &&
       argument_count != function.parameter_count)) {
    resolution->status = PSX_CALL_TYPES_ARGUMENT_COUNT_MISMATCH;
    return;
  }
  resolution->status = PSX_CALL_TYPES_OK;
}

void psx_resolve_call_argument_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t function_qual_type,
    int argument_index,
    psx_qual_type_t argument_qual_type,
    int argument_is_null_pointer_constant,
    psx_call_argument_types_status_t *status) {
  if (!status) return;
  *status = PSX_CALL_ARGUMENT_TYPES_INVALID;
  if (!semantic_context || argument_index < 0 ||
      function_qual_type.type_id == PSX_TYPE_ID_INVALID ||
      argument_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return;

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t function = {0};
  if (!psx_semantic_type_table_describe(
          types, function_qual_type.type_id, &function) ||
      function.kind != PSX_TYPE_FUNCTION)
    return;

  /* A declaration without a prototype and variadic arguments beyond the
   * fixed parameter list are checked only after the default argument
   * promotions.  This rule validates the arguments that have a declared
   * parameter type. */
  if (!function.has_function_prototype ||
      argument_index >= function.parameter_count) {
    *status = PSX_CALL_ARGUMENT_TYPES_OK;
    return;
  }

  psx_qual_type_t parameter_type =
      psx_semantic_type_table_parameter(
          types, function_qual_type.type_id, argument_index);
  if (parameter_type.type_id == PSX_TYPE_ID_INVALID) return;

  /* C11 6.5.2.2p2 compares an argument with the unqualified version of its
   * parameter type.  Atomic qualification remains semantically significant
   * to the canonical assignment rule. */
  parameter_type.qualifiers &= PSX_TYPE_QUALIFIER_ATOMIC;
  psx_assignment_types_resolution_t assignment;
  psx_resolve_assignment_qual_types_in(
      semantic_context, parameter_type, argument_qual_type,
      argument_is_null_pointer_constant, &assignment);
  switch (assignment.status) {
    case PSX_ASSIGNMENT_TYPES_OK:
      *status = PSX_CALL_ARGUMENT_TYPES_OK;
      return;
    case PSX_ASSIGNMENT_TYPES_INCOMPATIBLE:
      *status = PSX_CALL_ARGUMENT_TYPES_INCOMPATIBLE;
      return;
    case PSX_ASSIGNMENT_DISCARDS_QUALIFIERS:
      *status = PSX_CALL_ARGUMENT_TYPES_DISCARDS_QUALIFIERS;
      return;
    case PSX_ASSIGNMENT_TYPES_INVALID:
    case PSX_ASSIGNMENT_TARGET_NOT_MODIFIABLE:
      return;
  }
}
