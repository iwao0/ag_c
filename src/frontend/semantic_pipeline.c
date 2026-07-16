#include "semantic_pipeline.h"
#include "semantic_pipeline_internal.h"
#include "legacy_ast_api.h"

#include "../diag/diag.h"
#include "../parser/semantic_ctx.h"
#include "../semantic/resolution_work_tree_internal.h"
#include "../semantic/resolved_node_kind.h"
#include "../semantic/semantic_tree_resolution.h"
#include "../semantic/static_initializer_materialization.h"
#include "../semantic/typed_hir_materialization.h"

static psx_hir_node_id_t build_session_hir(
    ag_compilation_session_t *session,
    psx_resolution_work_tree_t *work_tree,
    const token_t *fallback_diag_tok) {
  psx_hir_module_t *hir = ag_compilation_session_hir_module(session);
  psx_resolved_hir_build_failure_t failure;
  psx_hir_node_id_t hir_root = PSX_HIR_NODE_ID_INVALID;
  if (psx_resolution_work_tree_build_typed_hir(
          work_tree, &failure)) {
    const psx_typed_hir_tree_t *typed_tree =
        psx_resolution_work_tree_typed_hir(work_tree);
    hir_root = psx_typed_hir_tree_emit(
        hir, typed_tree, &failure);
  }
  if (hir_root != PSX_HIR_NODE_ID_INVALID) return hir_root;
  ag_diagnostic_context_t *diagnostics =
      ag_compilation_session_diagnostic_context(session);
  if (fallback_diag_tok) {
    diag_emit_tokf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        fallback_diag_tok,
        "%s: Typed HIR build failed (status %d, node kind %d)",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
        (int)failure.status, failure.source_node_kind);
  }
  diag_emit_internalf_in(
      diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
      "%s: Typed HIR build failed (status %d, node kind %d)",
      diag_message_for_in(
          diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
      (int)failure.status, failure.source_node_kind);
  return PSX_HIR_NODE_ID_INVALID;
}

psx_resolution_work_tree_t *
psx_frontend_resolve_function_work_tree_in_session(
    ag_compilation_session_t *session,
    const node_t *syntax_function, const token_t *fallback_diag_tok,
    psx_hir_node_id_t *hir_root) {
  if (hir_root) *hir_root = PSX_HIR_NODE_ID_INVALID;
  if (!ag_compilation_session_is_complete(session) ||
      !syntax_function || syntax_function->kind != ND_FUNCDEF ||
      !hir_root) {
    return NULL;
  }
  psx_resolution_work_tree_t *work_tree =
      psx_resolution_work_tree_create_from_syntax(
      ag_compilation_session_arena_context(session), syntax_function);
  if (!work_tree) {
    ag_diagnostic_context_t *diagnostics =
        ag_compilation_session_diagnostic_context(session);
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: could not create resolver working tree",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED));
    return NULL;
  }
  if (!psx_resolve_function_semantic_tree_in_contexts(
      ag_compilation_session_semantic_context(session),
      ag_compilation_session_global_registry(session),
      ag_compilation_session_local_registry(session),
      ag_compilation_session_lowering_context(session),
      ag_compilation_session_options_view(session),
      work_tree, fallback_diag_tok))
    return NULL;
  *hir_root = build_session_hir(
      session, work_tree, fallback_diag_tok);
  if (*hir_root == PSX_HIR_NODE_ID_INVALID) return NULL;
  return work_tree;
}

int psx_frontend_resolve_function_to_hir_in_session(
    ag_compilation_session_t *session,
    const node_t *syntax_function, const token_t *fallback_diag_tok,
    psx_hir_node_id_t *hir_root) {
  return psx_frontend_resolve_function_work_tree_in_session(
             session, syntax_function, fallback_diag_tok, hir_root)
             ? 1 : 0;
}

static psx_resolution_work_tree_t *resolve_expression_work_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok) {
  if (!semantic_context || !syntax_expression) return NULL;
  psx_resolution_work_tree_t *work_tree =
      psx_resolution_work_tree_create_from_syntax(
          ps_ctx_arena(semantic_context), syntax_expression);
  if (!work_tree) {
    ag_diagnostic_context_t *diagnostics =
        ps_ctx_diagnostics(semantic_context);
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: could not create expression resolver working tree",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED));
    return NULL;
  }
  return psx_resolve_expression_semantic_tree_in_contexts(
             semantic_context, global_registry, local_registry,
             lowering_context, options, work_tree,
             fallback_diag_tok)
             ? work_tree : NULL;
}

static void diagnose_typed_expression_hir_failure(
    psx_semantic_context_t *semantic_context,
    const token_t *fallback_diag_tok,
    const psx_resolved_hir_build_failure_t *failure) {
  if (!semantic_context || !failure) return;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  if (fallback_diag_tok) {
    diag_emit_tokf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        fallback_diag_tok,
        "%s: expression Typed HIR build failed (status %d, node kind %d)",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
        (int)failure->status, failure->source_node_kind);
  }
  diag_emit_internalf_in(
      diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
      "%s: expression Typed HIR build failed (status %d, node kind %d)",
      diag_message_for_in(
          diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
      (int)failure->status, failure->source_node_kind);
}

int psx_frontend_resolve_expression_to_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok,
    psx_frontend_expression_hir_t *result) {
  if (result) {
    *result = (psx_frontend_expression_hir_t){
        .root = PSX_HIR_NODE_ID_INVALID,
    };
  }
  if (!result) return 0;
  psx_resolution_work_tree_t *work_tree =
      resolve_expression_work_tree_in_contexts(
          semantic_context, global_registry, local_registry,
          lowering_context, options, syntax_expression,
          fallback_diag_tok);
  if (!work_tree) return 0;

  psx_resolved_hir_build_failure_t failure;
  if (!psx_resolution_work_tree_build_typed_hir(
          work_tree, &failure)) {
    diagnose_typed_expression_hir_failure(
        semantic_context, fallback_diag_tok, &failure);
    return 0;
  }
  psx_hir_module_t *module = psx_hir_module_create();
  if (!module) return 0;
  psx_hir_node_id_t root = psx_typed_hir_tree_emit(
      module, psx_resolution_work_tree_typed_hir(work_tree), &failure);
  if (root == PSX_HIR_NODE_ID_INVALID) {
    diagnose_typed_expression_hir_failure(
        semantic_context, fallback_diag_tok, &failure);
    psx_hir_module_destroy(module);
    return 0;
  }
  result->module = module;
  result->root = root;
  return 1;
}

void psx_frontend_expression_hir_dispose(
    psx_frontend_expression_hir_t *expression) {
  if (!expression) return;
  psx_hir_module_destroy(expression->module);
  *expression = (psx_frontend_expression_hir_t){
      .root = PSX_HIR_NODE_ID_INVALID,
  };
}

static node_t *analyze_expression_compatibility_ast_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok) {
  psx_resolution_work_tree_t *work_tree =
      resolve_expression_work_tree_in_contexts(
          semantic_context, global_registry, local_registry,
          lowering_context, options, syntax_expression,
          fallback_diag_tok);
  if (!work_tree) return NULL;
  return psx_resolution_work_tree_export_compatibility_ast(work_tree);
}

node_t *psx_frontend_legacy_analyze_expression_ast_in_session(
    ag_compilation_session_t *session,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok) {
  if (!ag_compilation_session_is_complete(session))
    return (node_t *)syntax_expression;
  return analyze_expression_compatibility_ast_in_contexts(
      ag_compilation_session_semantic_context(session),
      ag_compilation_session_global_registry(session),
      ag_compilation_session_local_registry(session),
      ag_compilation_session_lowering_context(session),
      ag_compilation_session_options_view(session),
      syntax_expression, fallback_diag_tok);
}

static psx_resolution_work_tree_t *
resolve_initializer_work_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax, const token_t *fallback_diag_tok) {
  if (!semantic_context || !syntax) return NULL;
  psx_resolution_work_tree_t *work_tree =
      psx_resolution_work_tree_create_from_syntax(
          ps_ctx_arena(semantic_context), syntax);
  if (!work_tree) {
    ag_diagnostic_context_t *diagnostics =
        ps_ctx_diagnostics(semantic_context);
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: could not create initializer resolver working tree",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED));
    return NULL;
  }
  return psx_resolve_initializer_semantic_tree_in_contexts(
             semantic_context, global_registry, local_registry,
             lowering_context, options, work_tree,
             fallback_diag_tok)
             ? work_tree : NULL;
}

int psx_frontend_resolve_static_aggregate_initializer_plan_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const psx_type_t *type, const node_t *syntax,
    const token_t *fallback_diag_tok,
    psx_static_aggregate_initializer_plan_t *plan) {
  if (plan) *plan = (psx_static_aggregate_initializer_plan_t){0};
  if (!type || !plan) return 0;
  psx_resolution_work_tree_t *work_tree =
      resolve_initializer_work_tree_in_contexts(
          semantic_context, global_registry, local_registry,
          lowering_context, options, syntax, fallback_diag_tok);
  return psx_materialize_static_aggregate_initializer_plan(
      work_tree, global_registry, lowering_context, type,
      (token_t *)fallback_diag_tok, plan);
}
