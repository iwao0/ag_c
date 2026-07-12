#include "function_parameter_resolution.h"

#include "../diag/diag.h"
#include "../parser/diag.h"
#include "../parser/enum_const.h"
#include "../parser/semantic_ctx.h"

void psx_resolve_declarator_syntax(
    const psx_parsed_declarator_t *parsed,
    psx_declarator_shape_t *shape, int *bit_width,
    const psx_decl_syntax_resolution_context_t *context) {
  *shape = parsed->declarator_shape;
  for (int i = 0; i < parsed->array_bound_count; i++) {
    const psx_parsed_array_bound_t *bound = &parsed->array_bounds[i];
    long long value = ps_eval_parsed_enum_const_expr(
        bound->expression.start, bound->expression.end);
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
  for (int i = 0; i < parsed->function_suffix_count; i++) {
    const psx_parsed_function_suffix_t *suffix =
        &parsed->function_suffixes[i];
    if (suffix->declarator_op_index < 0 ||
        suffix->declarator_op_index >= shape->count ||
        shape->ops[suffix->declarator_op_index].kind !=
            PSX_DECL_OP_FUNCTION ||
        !suffix->parameters) {
      ps_diag_ctx(parsed->diagnostic_token, "declarator-resolution",
                   "invalid deferred function suffix target");
    }
    psx_resolve_function_parameter_types(
        suffix->parameters,
        &shape->ops[suffix->declarator_op_index],
        parsed->diagnostic_token, context);
  }
  if (bit_width) *bit_width = 0;
  if (parsed->has_bitfield) {
    long long value = ps_eval_parsed_enum_const_expr(
        parsed->bit_width_expression.start,
        parsed->bit_width_expression.end);
    if (bit_width) *bit_width = value > 0 ? (int)value : 0;
  }
}

void psx_resolve_function_parameter_types(
    psx_parsed_function_parameters_t *parameters,
    psx_declarator_op_t *function_op, token_t *diagnostic_token,
    const psx_decl_syntax_resolution_context_t *context) {
  if (!parameters || !function_op ||
      function_op->kind != PSX_DECL_OP_FUNCTION) {
    ps_diag_ctx(diagnostic_token, "declarator-resolution",
                 "invalid function parameter syntax target");
  }
  int resolved_count = 0;
  ps_ctx_enter_block_scope();
  for (int i = 0; i < parameters->count; i++) {
    psx_parsed_function_parameter_t *parameter = &parameters->items[i];
    psx_type_t *base = psx_resolve_decl_specifier_syntax(
        &parameter->specifier, context);
    if (!base) {
      ps_diag_ctx(parameter->specifier.diagnostic_token, "param", "%s",
                   diag_message_for(DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
    }
    psx_declarator_shape_t parameter_shape;
    int ignored_bit_width = 0;
    psx_resolve_declarator_syntax(
        &parameter->declarator, &parameter_shape, &ignored_bit_width,
        context);
    psx_type_t *type = ps_type_apply_declarator_shape(base, &parameter_shape);
    if (parameters->count == 1 && type && type->kind == PSX_TYPE_VOID &&
        parameter_shape.count == 0) {
      resolved_count = 0;
      break;
    }
    if (resolved_count < 16) {
      function_op->function_param_types[resolved_count] =
          ps_type_adjust_parameter_type(type);
    }
    resolved_count++;
  }
  ps_ctx_leave_block_scope();
  psx_set_resolved_function_parameter_types(
      function_op, function_op->function_param_types,
      resolved_count, parameters->is_variadic);
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
