#include "legacy_syntax_diagnostics.h"
#include "semantic_tree_resolution_test_support.h"

#include "../diag/diag.h"
#include "../lowering/semantic_lowering_pass.h"
#include "../parser/decl.h"
#include "../parser/function_definition_syntax.h"
#include "../parser/semantic_ctx.h"
#include "control_flow_validation.h"
#include "function_definition_resolution.h"
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

static int finalize_expression_compatibility_tree(
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
  return 1;
}

static int resolve_nonfunction_compatibility_tree(
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
         finalize_expression_compatibility_tree(
             semantic_context, work_tree, fallback_diag_tok,
             is_initializer, root);
}

int psx_resolve_expression_compatibility_work_tree_for_test_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    psx_resolution_work_tree_t *work_tree,
    const token_t *fallback_diag_tok) {
  return resolve_nonfunction_compatibility_tree(
             semantic_context, global_registry, local_registry,
             lowering_context, options, work_tree,
             fallback_diag_tok, 0) &&
         materialize_resolved_tree(
             semantic_context, work_tree, fallback_diag_tok);
}

static int resolve_function_compatibility_tree_in_contexts(
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
      psx_resolution_node_kind(function) != ND_FUNCDEF)
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
  return 1;
}

static psx_resolution_work_tree_t *resolve_function_compatibility_tree(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok) {
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context || !lowering_context || !options ||
      !syntax_function || !syntax_function->body)
    return NULL;
  psx_resolution_work_tree_t *work_tree =
      psx_resolution_work_tree_create_from_syntax(
          ps_ctx_arena(semantic_context), syntax_function->body);
  node_t *body = mutable_compatibility_root(work_tree);
  if (!work_tree || !body) return NULL;
  psx_parsed_function_definition_t work_definition =
      *syntax_function;
  work_definition.body = body;
  node_function_definition_t *function =
      psx_prepare_function_definition_resolution_in_contexts(
          semantic_context, global_registry, local_registry,
          runtime_context, lowering_context, &work_definition);
  if (!function ||
      !psx_resolution_work_tree_replace_compatibility_root(
          work_tree, &function->base) ||
      !resolve_function_compatibility_tree_in_contexts(
          semantic_context, global_registry, local_registry,
          lowering_context, options, work_tree,
          fallback_diag_tok))
    return NULL;
  return work_tree;
}

int psx_resolve_parsed_function_compatibility_for_test_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok,
    psx_function_compatibility_test_result_t *result) {
  if (result)
    *result = (psx_function_compatibility_test_result_t){0};
  if (!result) return 0;
  psx_resolution_work_tree_t *work_tree =
      resolve_function_compatibility_tree(
          semantic_context, global_registry, local_registry,
          runtime_context, lowering_context, options,
          syntax_function, fallback_diag_tok);
  if (!work_tree ||
      !materialize_resolved_tree(
          semantic_context, work_tree, fallback_diag_tok))
    return 0;
  result->typed_hir = psx_resolution_work_tree_typed_hir(work_tree);
  result->compatibility_root = mutable_compatibility_root(work_tree);
  return result->typed_hir && result->compatibility_root;
}

int psx_legacy_syntax_diagnostics_accept_function_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok) {
  return resolve_function_compatibility_tree(
             semantic_context, global_registry, local_registry,
             runtime_context, lowering_context, options,
             syntax_function, fallback_diag_tok) != NULL;
}

int psx_legacy_syntax_diagnostics_accept_nonfunction_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax,
    const token_t *fallback_diag_tok,
    int is_initializer) {
  psx_resolution_work_tree_t *work_tree =
      psx_resolution_work_tree_create_from_syntax(
          ps_ctx_arena(semantic_context), syntax);
  if (!work_tree) {
    ag_diagnostic_context_t *diagnostics =
        ps_ctx_diagnostics(semantic_context);
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: could not create %s compatibility diagnostic tree",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
        is_initializer ? "initializer" : "expression");
    return 0;
  }
  return resolve_nonfunction_compatibility_tree(
      semantic_context, global_registry, local_registry,
      lowering_context, options, work_tree,
      fallback_diag_tok, is_initializer);
}
