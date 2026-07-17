#include "semantic_tree_resolution.h"

#include "../diag/diag.h"
#include "../lowering/semantic_lowering_pass.h"
#include "../parser/decl.h"
#include "../parser/semantic_ctx.h"
#include "control_flow_validation.h"
#include "identifier_binding.h"
#include "local_declaration_tree_resolution.h"
#include "lowered_tree_validation.h"
#include "lvar_usage_analysis.h"
#include "resolution_work_tree_internal.h"
#include "resolved_function.h"
#include "resolved_node_kind.h"
#include "semantic_diagnostics.h"
#include "semantic_invariants.h"
#include "semantic_pass.h"
#include "typed_hir_materialization.h"

static node_t *mutable_compatibility_root(
    psx_resolution_work_tree_t *work_tree) {
  return psx_resolution_work_tree_compatibility_root_mut(work_tree);
}

static int advance_with_compatibility_root(
    psx_resolution_work_tree_t *work_tree,
    psx_resolution_work_phase_t expected,
    psx_resolution_work_phase_t next, node_t *root) {
  return psx_resolution_work_tree_replace_compatibility_root(
             work_tree, root) &&
         psx_resolution_work_tree_advance(
             work_tree, expected, next);
}

static int materialize_resolved_tree(
    psx_semantic_context_t *semantic_context,
    psx_resolution_work_tree_t *work_tree,
    const token_t *fallback_diag_tok) {
  psx_resolved_hir_build_failure_t failure;
  if (psx_resolution_work_tree_materialize_typed_hir(
          work_tree, semantic_context, &failure))
    return 1;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  if (fallback_diag_tok) {
    diag_emit_tokf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        fallback_diag_tok,
        "%s: semantic tree materialization failed (status %d, node kind %d)",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
        (int)failure.status, failure.source_node_kind);
  } else {
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: semantic tree materialization failed (status %d, node kind %d)",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
        (int)failure.status, failure.source_node_kind);
  }
  return 0;
}

static int prepare_bound_tree(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    psx_resolution_work_tree_t *work_tree,
    const token_t *fallback_diag_tok,
    int is_initializer, node_t **root) {
  *root = mutable_compatibility_root(work_tree);
  if (!semantic_context || !global_registry || !local_registry ||
      !lowering_context || !options || !*root)
    return 0;
  if (!psx_resolve_local_declaration_syntax_tree_in_contexts(
          semantic_context, global_registry, local_registry,
          lowering_context, options, root))
    return 0;
  *root = is_initializer
              ? psx_bind_identifier_initializer_tree_in_contexts(
                    semantic_context, global_registry, local_registry,
                    *root, fallback_diag_tok)
              : psx_bind_identifier_tree_in_contexts(
                    semantic_context, global_registry, local_registry,
                    *root, fallback_diag_tok);
  return *root &&
         advance_with_compatibility_root(
             work_tree, PSX_RESOLUTION_WORK_CLONED,
             PSX_RESOLUTION_WORK_BOUND, *root);
}

static int resolve_typed_tree(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_resolution_work_tree_t *work_tree,
    const token_t *fallback_diag_tok,
    int is_initializer, node_t *root,
    node_function_definition_t *current_function) {
  if (is_initializer) {
    psx_semantic_resolve_initializer_tree_in_contexts(
        semantic_context, global_registry, local_registry,
        root, current_function, fallback_diag_tok);
  } else {
    psx_semantic_resolve_tree_in_contexts(
        semantic_context, global_registry, local_registry,
        root, current_function, fallback_diag_tok);
  }
  psx_require_available_semantic_tree_types_interned(
      semantic_context, ps_ctx_diagnostics(semantic_context), root,
      fallback_diag_tok);
  return advance_with_compatibility_root(
      work_tree, PSX_RESOLUTION_WORK_BOUND,
      PSX_RESOLUTION_WORK_TYPED, root);
}

static node_t *lower_tree(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const token_t *fallback_diag_tok,
    int is_initializer, node_t *root) {
  return is_initializer
             ? psx_lower_semantic_initializer_syntax_in_contexts(
                   semantic_context, global_registry, local_registry,
                   lowering_context, options, root, fallback_diag_tok)
             : psx_lower_semantic_tree_in_contexts(
                   semantic_context, global_registry, local_registry,
                   lowering_context, options, root, fallback_diag_tok);
}

static int finalize_expression_tree(
    psx_semantic_context_t *semantic_context,
    psx_resolution_work_tree_t *work_tree,
    const token_t *fallback_diag_tok,
    int is_initializer, node_t *root) {
  psx_validate_lowered_tree_in_context(
      semantic_context, root, fallback_diag_tok);
  if (is_initializer) {
    psx_require_semantic_initializer_has_interned_expression_types(
        semantic_context, ps_ctx_diagnostics(semantic_context), root,
        fallback_diag_tok);
    psx_require_semantic_initializer_has_canonical_expression_types(
        ps_ctx_diagnostics(semantic_context), root, fallback_diag_tok);
  } else {
    psx_require_semantic_tree_has_interned_expression_types(
        semantic_context, ps_ctx_diagnostics(semantic_context), root,
        fallback_diag_tok);
    psx_require_semantic_tree_has_canonical_expression_types(
        ps_ctx_diagnostics(semantic_context), root, fallback_diag_tok);
  }
  if (!advance_with_compatibility_root(
          work_tree, PSX_RESOLUTION_WORK_LOWERED,
          PSX_RESOLUTION_WORK_FINALIZED, root))
    return 0;
  if (is_initializer) return 1;
  return materialize_resolved_tree(
      semantic_context, work_tree, fallback_diag_tok);
}

static int resolve_nonfunction_tree(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    psx_resolution_work_tree_t *work_tree,
    const token_t *fallback_diag_tok,
    int is_initializer) {
  node_t *root = NULL;
  if (!prepare_bound_tree(
          semantic_context, global_registry, local_registry,
          lowering_context, options, work_tree, fallback_diag_tok,
          is_initializer, &root) ||
      !resolve_typed_tree(
          semantic_context, global_registry, local_registry,
          work_tree, fallback_diag_tok, is_initializer, root, NULL))
    return 0;
  root = lower_tree(
      semantic_context, global_registry, local_registry,
      lowering_context, options, fallback_diag_tok,
      is_initializer, root);
  return root &&
         advance_with_compatibility_root(
             work_tree, PSX_RESOLUTION_WORK_TYPED,
             PSX_RESOLUTION_WORK_LOWERED, root) &&
         finalize_expression_tree(
             semantic_context, work_tree, fallback_diag_tok,
             is_initializer, root);
}

int psx_resolve_function_semantic_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    psx_resolution_work_tree_t *work_tree,
    const token_t *fallback_diag_tok) {
  node_t *function = NULL;
  if (!prepare_bound_tree(
          semantic_context, global_registry, local_registry,
          lowering_context, options, work_tree, fallback_diag_tok,
          0, &function) ||
      function->kind != ND_FUNCDEF)
    return 0;
  node_function_definition_t *current_function =
      (node_function_definition_t *)function;
  if (!resolve_typed_tree(
          semantic_context, global_registry, local_registry,
          work_tree, fallback_diag_tok, 0, function,
          current_function))
    return 0;
  psx_validate_control_flow(
      semantic_context, function, fallback_diag_tok);
  function = lower_tree(
      semantic_context, global_registry, local_registry,
      lowering_context, options, fallback_diag_tok, 0, function);
  if (!function ||
      !advance_with_compatibility_root(
          work_tree, PSX_RESOLUTION_WORK_TYPED,
          PSX_RESOLUTION_WORK_LOWERED, function))
    return 0;
  psx_validate_lowered_tree_in_context(
      semantic_context, function, fallback_diag_tok);
  current_function->lvars = ps_decl_get_locals_in(local_registry);
  psx_emit_semantic_warnings(
      semantic_context, function, current_function,
      fallback_diag_tok);
  psx_emit_unreachable_warnings(
      semantic_context, function, fallback_diag_tok);
  psx_require_semantic_tree_has_interned_expression_types(
      semantic_context, ps_ctx_diagnostics(semantic_context), function,
      fallback_diag_tok);
  psx_require_semantic_tree_has_canonical_expression_types(
      ps_ctx_diagnostics(semantic_context), function, fallback_diag_tok);
  psx_analyze_function_lvar_usage_in(
      ps_ctx_diagnostics(semantic_context), local_registry,
      current_function, fallback_diag_tok);
  if (!advance_with_compatibility_root(
          work_tree, PSX_RESOLUTION_WORK_LOWERED,
          PSX_RESOLUTION_WORK_FINALIZED, function))
    return 0;
  return materialize_resolved_tree(
      semantic_context, work_tree, fallback_diag_tok);
}

int psx_resolve_expression_semantic_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    psx_resolution_work_tree_t *work_tree,
    const token_t *fallback_diag_tok) {
  return resolve_nonfunction_tree(
      semantic_context, global_registry, local_registry,
      lowering_context, options, work_tree, fallback_diag_tok, 0);
}

int psx_resolve_initializer_semantic_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    psx_resolution_work_tree_t *work_tree,
    const token_t *fallback_diag_tok) {
  return resolve_nonfunction_tree(
      semantic_context, global_registry, local_registry,
      lowering_context, options, work_tree, fallback_diag_tok, 1);
}
