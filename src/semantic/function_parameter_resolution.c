#include "function_parameter_resolution.h"

#include "../diag/diag.h"
#include "../parser/diag.h"
#include "../parser/semantic_ctx.h"

void psx_resolve_declarator_syntax(
    const psx_parsed_declarator_t *parsed,
    psx_declarator_shape_t *shape, int *bit_width) {
  *shape = parsed->declarator_shape;
  for (int i = 0; i < parsed->array_bound_count; i++) {
    const psx_parsed_array_bound_t *bound = &parsed->array_bounds[i];
    if (!bound->expression.has_constant_value) {
      ps_diag_ctx(bound->expression.start, "declarator-resolution",
                   "constant array bound syntax was not prepared");
    }
    long long value = bound->expression.constant_value;
    if (value < 0) {
      ps_diag_ctx(bound->expression.start, "decl", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
    }
    if (value == 0) {
      ps_ctx_record_unsupported_gnu_extension_warning(
          bound->expression.start, "zero-length array");
    }
    if (bound->declarator_op_index < 0 ||
        bound->declarator_op_index >= shape->count ||
        shape->ops[bound->declarator_op_index].kind != PSX_DECL_OP_ARRAY) {
      ps_diag_ctx(bound->expression.start, "declarator-resolution",
                   "invalid deferred array bound target");
    }
    shape->ops[bound->declarator_op_index].array_len = (int)value;
    shape->ops[bound->declarator_op_index].is_incomplete_array = 0;
  }
  if (bit_width) *bit_width = 0;
  if (parsed->has_bitfield) {
    if (!parsed->bit_width_expression.has_constant_value) {
      ps_diag_ctx(parsed->bit_width_expression.start,
                   "declarator-resolution",
                   "bit-field width syntax was not prepared");
    }
    long long value = parsed->bit_width_expression.constant_value;
    if (bit_width) *bit_width = value > 0 ? (int)value : 0;
  }
}

void psx_set_resolved_function_parameter_types(
    psx_declarator_op_t *function_op, psx_type_t **parameter_types,
    int parameter_count, int is_variadic) {
  if (!function_op || function_op->kind != PSX_DECL_OP_FUNCTION) return;
  function_op->has_canonical_function_params = 1;
  function_op->function_param_count = parameter_count;
  function_op->function_is_variadic = is_variadic;
  for (int i = 0; i < parameter_count && i < 16; i++)
    function_op->function_param_types[i] = parameter_types[i];
  psx_type_t *projection = ps_type_new_function(
      ps_type_new(PSX_TYPE_VOID), (psx_decl_funcptr_sig_t){0});
  ps_type_set_function_params(
      projection, parameter_types, parameter_count, is_variadic);
  function_op->funcptr_sig.function.callable.signature =
      ps_type_funcptr_signature(projection).function.callable.signature;
}
