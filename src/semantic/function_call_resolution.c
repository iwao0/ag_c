#include "function_call_resolution.h"

#include "../parser/ast.h"
#include "../parser/semantic_ctx.h"

#include <string.h>

psx_builtin_call_kind_t psx_function_call_builtin_kind(
    const node_function_call_t *call) {
  static const char builtin_expect[] = "__builtin_expect";
  if (!call || !call->callee ||
      call->callee->kind != ND_IDENTIFIER)
    return PSX_BUILTIN_CALL_NONE;
  const node_identifier_t *identifier =
      (const node_identifier_t *)call->callee;
  if (identifier->name_len ==
          (int)(sizeof(builtin_expect) - 1) &&
      memcmp(
          identifier->name, builtin_expect,
          sizeof(builtin_expect) - 1) == 0)
    return PSX_BUILTIN_CALL_EXPECT;
  return PSX_BUILTIN_CALL_NONE;
}

const node_t *psx_builtin_expect_value_operand(
    const node_function_call_t *call) {
  return psx_function_call_builtin_kind(call) ==
                 PSX_BUILTIN_CALL_EXPECT &&
             call->argument_count == 2 && call->arguments &&
             call->arguments[0] && call->arguments[1]
         ? call->arguments[0] : NULL;
}

psx_qual_type_t psx_resolve_function_reference_qual_type(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t function_qual_type) {
  psx_type_shape_t shape = {0};
  if (!semantic_context ||
      !psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(semantic_context),
          function_qual_type.type_id, &shape) ||
      shape.kind != PSX_TYPE_FUNCTION) {
    return (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  }
  return ps_ctx_intern_pointer_to_qual_type_in(
      semantic_context, function_qual_type);
}
