#include "function_parameter_resolution.h"
#include "declarator_bound_resolution.h"
#include "../parser/declarator_shape_builder.h"

#include "../diag/diag.h"
#include "../parser/diag.h"
#include "../parser/semantic_ctx.h"

#include <limits.h>

void psx_resolve_declarator_syntax_in_context(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *parsed,
    psx_declarator_shape_t *shape, int *bit_width) {
  if (!parsed || !shape ||
      !ps_declarator_shape_copy_in(
          ps_ctx_arena(semantic_context),
          shape, &parsed->declarator_shape)) {
    ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), parsed ? parsed->diagnostic_token : NULL,
                "declarator-resolution",
                "invalid declarator shape");
  }
  for (int i = 0; i < parsed->array_bound_count; i++) {
    const psx_parsed_array_bound_t *bound = &parsed->array_bounds[i];
    psx_declarator_bound_resolution_t resolution;
    if (!bound->expression.node ||
        !psx_resolve_declarator_bound_in_contexts(
            semantic_context, global_registry, local_registry,
            bound->expression.node, NULL, bound->expression.start,
            &resolution) ||
        !resolution.is_constant) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(semantic_context), bound->expression.start,
          "declarator-resolution",
          "array bound is not an integer constant expression");
    }
    long long value = resolution.constant_value;
    if (value < 0) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), bound->expression.start, "decl", "%s",
                   diag_message_for_in(ps_ctx_diagnostics(semantic_context),
                       DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
    }
    if (value == 0) {
      ps_ctx_record_unsupported_gnu_extension_in(
          semantic_context,
          bound->expression.start, "zero-length array");
    }
    if (!ps_declarator_shape_set_array_bound(
            shape, bound->declarator_op_index, (int)value, 0)) {
      ps_diag_ctx_in(ps_ctx_diagnostics(semantic_context), bound->expression.start, "declarator-resolution",
                   "invalid deferred array bound target");
    }
  }
  if (bit_width) *bit_width = 0;
  if (parsed->has_bitfield) {
    psx_declarator_bound_resolution_t resolution;
    if (!parsed->bit_width_expression.node ||
        !psx_resolve_declarator_bound_in_contexts(
            semantic_context, global_registry, local_registry,
            parsed->bit_width_expression.node, NULL,
            parsed->bit_width_expression.start, &resolution) ||
        !resolution.is_constant) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(semantic_context),
          parsed->bit_width_expression.start, "declarator-resolution",
          "bit-field width is not an integer constant expression");
    }
    long long value = resolution.constant_value;
    if (value < INT_MIN || value > INT_MAX) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(semantic_context),
          parsed->bit_width_expression.start, "declarator-resolution",
          "bit-field width is outside the supported integer range");
      value = 0;
    }
    if (bit_width) *bit_width = (int)value;
  }
}

void psx_set_resolved_function_parameter_qual_types(
    arena_context_t *arena_context,
    psx_declarator_op_t *function_op,
    const psx_qual_type_t *parameter_qual_types,
    int parameter_count, int is_variadic, int has_prototype) {
  if (!function_op || function_op->kind != PSX_DECL_OP_FUNCTION) return;
  (void)ps_declarator_op_set_function_param_qual_types_in(
      arena_context, function_op,
      parameter_qual_types, parameter_count, is_variadic,
      has_prototype);
}
