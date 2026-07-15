#include "function_parameter_resolution.h"
#include "../parser/declarator_shape_builder.h"

#include "../diag/diag.h"
#include "../parser/diag.h"
#include "../parser/semantic_ctx.h"

void psx_resolve_declarator_syntax_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_parsed_declarator_t *parsed,
    psx_declarator_shape_t *shape, int *bit_width) {
  if (!parsed || !shape ||
      !ps_declarator_shape_copy_in(
          ps_ctx_arena(semantic_context),
          shape, &parsed->declarator_shape)) {
    ps_diag_ctx(parsed ? parsed->diagnostic_token : NULL,
                "declarator-resolution",
                "invalid declarator shape");
  }
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
      ps_ctx_record_unsupported_gnu_extension_warning_in(
          semantic_context,
          bound->expression.start, "zero-length array");
    }
    if (!ps_declarator_shape_set_array_bound(
            shape, bound->declarator_op_index, (int)value, 0)) {
      ps_diag_ctx(bound->expression.start, "declarator-resolution",
                   "invalid deferred array bound target");
    }
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
    arena_context_t *arena_context,
    psx_declarator_op_t *function_op,
    const psx_type_t *const *parameter_types,
    int parameter_count, int is_variadic) {
  if (!function_op || function_op->kind != PSX_DECL_OP_FUNCTION) return;
  (void)ps_declarator_op_set_function_params_in(
      arena_context, function_op,
      parameter_types, parameter_count, is_variadic);
}
