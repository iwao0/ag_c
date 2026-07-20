#include "declarator_bound_resolution.h"

#include "../diag/diag.h"
#include "../parser/semantic_ctx.h"
#include "syntax_typed_hir_resolution.h"
#include "typed_hir_materialization.h"

int psx_resolve_declarator_bound_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *syntax_expression,
    const psx_scope_lookup_point_t *lookup_point,
    const token_t *fallback_diag_tok,
    psx_declarator_bound_resolution_t *resolution) {
  if (resolution) *resolution = (psx_declarator_bound_resolution_t){0};
  if (!semantic_context || !global_registry || !local_registry ||
      !syntax_expression || !resolution)
    return 0;
  (void)fallback_diag_tok;
  psx_resolved_hir_build_failure_t failure;
  psx_syntax_integer_constant_result_t constant_result;
  psx_syntax_typed_hir_resolution_status_t status =
      psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts(
          semantic_context, global_registry, local_registry,
          lookup_point, syntax_expression, &resolution->typed_expression,
          &constant_result, &failure);
  if (status != PSX_SYNTAX_TYPED_HIR_RESOLVED ||
      !resolution->typed_expression) {
    ag_diagnostic_context_t *diagnostics =
        ps_ctx_diagnostics(semantic_context);
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: direct declarator bound Typed HIR resolution failed "
        "(status %d, node kind %d)",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
        (int)status, failure.source_node_kind);
    return 0;
  }
  resolution->constant_value = constant_result.value;
  resolution->is_constant = constant_result.is_constant;
  return 1;
}
