#include "declarator_bound_resolution.h"

#include "../diag/diag.h"
#include "../parser/semantic_ctx.h"
#include "constant_expression.h"
#include "identifier_binding.h"
#include "lvar_usage_analysis.h"
#include "resolution_work_tree_internal.h"
#include "semantic_invariants.h"
#include "semantic_pass.h"
#include "typed_hir_tree_materialization.h"

int psx_resolve_declarator_bound_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *syntax_expression,
    const psx_local_lookup_point_t *lookup_point,
    const token_t *fallback_diag_tok,
    psx_declarator_bound_resolution_t *resolution) {
  if (resolution) *resolution = (psx_declarator_bound_resolution_t){0};
  if (!semantic_context || !global_registry || !local_registry ||
      !syntax_expression || !resolution)
    return 0;
  psx_resolution_work_tree_t *work_tree =
      psx_resolution_work_tree_create_from_syntax(
          ps_ctx_arena(semantic_context), syntax_expression);
  node_t *expression =
      psx_resolution_work_tree_compatibility_root_mut(work_tree);
  if (!work_tree || !expression) return 0;
  expression = lookup_point
                   ? psx_bind_identifier_tree_at_lookup_point_in_contexts(
                         semantic_context, global_registry, local_registry,
                         *lookup_point, expression, fallback_diag_tok)
                   : psx_bind_identifier_tree_in_contexts(
                         semantic_context, global_registry, local_registry,
                         expression, fallback_diag_tok);
  if (!expression) return 0;
  psx_semantic_resolve_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      expression, NULL, fallback_diag_tok);
  psx_collect_lvar_usage_events_in(local_registry, expression, NULL);
  psx_require_available_semantic_tree_types_interned(
      semantic_context, ps_ctx_diagnostics(semantic_context),
      expression, fallback_diag_tok);
  psx_require_semantic_tree_has_interned_expression_types(
      semantic_context, ps_ctx_diagnostics(semantic_context),
      expression, fallback_diag_tok);
  psx_require_semantic_tree_has_canonical_expression_types(
      ps_ctx_diagnostics(semantic_context), expression,
      fallback_diag_tok);
  psx_resolved_hir_build_failure_t failure;
  resolution->typed_expression = psx_typed_hir_tree_materialize(
      expression, semantic_context, &failure);
  if (!resolution->typed_expression) {
    ag_diagnostic_context_t *diagnostics =
        ps_ctx_diagnostics(semantic_context);
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: declarator bound Typed HIR materialization failed "
        "(status %d, node kind %d)",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
        (int)failure.status, failure.source_node_kind);
    return 0;
  }
  int is_constant = 1;
  resolution->constant_value =
      psx_eval_const_int(expression, &is_constant);
  resolution->is_constant = is_constant ? 1 : 0;
  return 1;
}
