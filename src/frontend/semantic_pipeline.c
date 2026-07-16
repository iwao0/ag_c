#include "semantic_pipeline.h"
#include "semantic_pipeline_internal.h"

#include "../diag/diag.h"
#include "../lowering/semantic_lowering_pass.h"
#include "../parser/decl.h"
#include "../parser/global_registry.h"
#include "../parser/semantic_ctx.h"
#include "../semantic/control_flow_validation.h"
#include "../semantic/identifier_binding.h"
#include "../semantic/lvar_usage_analysis.h"
#include "../semantic/semantic_diagnostics.h"
#include "../semantic/semantic_invariants.h"
#include "../semantic/semantic_pass.h"
#include "../semantic/typed_hir_builder.h"
#include "../semantic/resolution_work_tree.h"

static psx_hir_node_id_t build_session_hir(
    ag_compilation_session_t *session, const node_t *root,
    const token_t *fallback_diag_tok) {
  psx_hir_module_t *hir = ag_compilation_session_hir_module(session);
  size_t roots_before = psx_hir_module_root_count(hir);
  psx_typed_hir_build_failure_t failure;
  if (psx_build_typed_hir_root(
          hir,
          ag_compilation_session_semantic_context(session),
          root, &failure)) {
    if (psx_hir_module_root_count(hir) == roots_before + 1)
      return psx_hir_module_root_at(hir, roots_before);
    return PSX_HIR_NODE_ID_INVALID;
  }
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

static node_t *analyze_function_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    node_t *function, const token_t *fallback_diag_tok) {
  if (!semantic_context || !local_registry ||
      !function || function->kind != ND_FUNCDEF) return NULL;
  function = psx_bind_identifier_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      function, fallback_diag_tok);
  node_function_definition_t *current_function =
      (node_function_definition_t *)function;
  psx_validate_control_flow(
      semantic_context, function, fallback_diag_tok);
  psx_semantic_resolve_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      function, current_function, fallback_diag_tok);
  psx_require_available_semantic_tree_types_interned(
      semantic_context, ps_ctx_diagnostics(semantic_context), function,
      fallback_diag_tok);
  function = psx_lower_semantic_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      lowering_context, options,
      function, fallback_diag_tok);
  psx_semantic_resolve_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      function, current_function, fallback_diag_tok);
  current_function->lvars = ps_decl_get_locals_in(local_registry);
  psx_emit_semantic_warnings(
      semantic_context, function, current_function,
      fallback_diag_tok);
  psx_emit_unreachable_warnings(
      semantic_context, function, fallback_diag_tok);
  psx_lower_implicit_conversions(
      lowering_context, function, current_function,
      fallback_diag_tok, options);
  psx_require_semantic_tree_has_interned_expression_types(
      semantic_context, ps_ctx_diagnostics(semantic_context), function,
      fallback_diag_tok);
  psx_require_semantic_tree_has_canonical_expression_types(
      ps_ctx_diagnostics(semantic_context), function, fallback_diag_tok);
  psx_analyze_function_lvar_usage_in(
      ps_ctx_diagnostics(semantic_context), local_registry,
      current_function, fallback_diag_tok);
  return function;
}

node_t *psx_frontend_resolve_function_work_tree_in_session(
    ag_compilation_session_t *session,
    const node_t *syntax_function, const token_t *fallback_diag_tok,
    psx_hir_node_id_t *hir_root) {
  if (hir_root) *hir_root = PSX_HIR_NODE_ID_INVALID;
  if (!ag_compilation_session_is_complete(session) ||
      !syntax_function || syntax_function->kind != ND_FUNCDEF ||
      !hir_root) {
    return NULL;
  }
  node_t *function = psx_clone_syntax_tree_for_resolution(
      ag_compilation_session_arena_context(session), syntax_function);
  if (!function) {
    ag_diagnostic_context_t *diagnostics =
        ag_compilation_session_diagnostic_context(session);
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: could not create resolver working tree",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED));
    return NULL;
  }
  function = analyze_function_in_contexts(
      ag_compilation_session_semantic_context(session),
      ag_compilation_session_global_registry(session),
      ag_compilation_session_local_registry(session),
      ag_compilation_session_lowering_context(session),
      ag_compilation_session_options_view(session),
      function, fallback_diag_tok);
  if (!function) return NULL;
  *hir_root = build_session_hir(
      session, function, fallback_diag_tok);
  if (*hir_root == PSX_HIR_NODE_ID_INVALID) return NULL;
  return function;
}

int psx_frontend_resolve_function_to_hir_in_session(
    ag_compilation_session_t *session,
    const node_t *syntax_function, const token_t *fallback_diag_tok,
    psx_hir_node_id_t *hir_root) {
  return psx_frontend_resolve_function_work_tree_in_session(
             session, syntax_function, fallback_diag_tok, hir_root)
             ? 1 : 0;
}

node_t *psx_frontend_analyze_expression_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok) {
  if (!semantic_context || !syntax_expression) return NULL;
  node_t *expression = psx_clone_syntax_tree_for_resolution(
      ps_ctx_arena(semantic_context), syntax_expression);
  if (!expression) {
    ag_diagnostic_context_t *diagnostics =
        ps_ctx_diagnostics(semantic_context);
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: could not create expression resolver working tree",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED));
    return NULL;
  }
  expression = psx_bind_identifier_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      expression, fallback_diag_tok);
  psx_semantic_resolve_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      expression, NULL, fallback_diag_tok);
  psx_require_available_semantic_tree_types_interned(
      semantic_context, ps_ctx_diagnostics(semantic_context), expression,
      fallback_diag_tok);
  expression = psx_lower_semantic_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      lowering_context, options,
      expression, fallback_diag_tok);
  psx_semantic_resolve_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      expression, NULL, fallback_diag_tok);
  psx_lower_implicit_conversions(
      lowering_context, expression, NULL, fallback_diag_tok, options);
  psx_require_semantic_tree_has_interned_expression_types(
      semantic_context, ps_ctx_diagnostics(semantic_context), expression,
      fallback_diag_tok);
  psx_require_semantic_tree_has_canonical_expression_types(
      ps_ctx_diagnostics(semantic_context), expression, fallback_diag_tok);
  return expression;
}

node_t *psx_frontend_analyze_expression_in_session(
    ag_compilation_session_t *session,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok) {
  if (!ag_compilation_session_is_complete(session))
    return (node_t *)syntax_expression;
  return psx_frontend_analyze_expression_in_contexts(
      ag_compilation_session_semantic_context(session),
      ag_compilation_session_global_registry(session),
      ag_compilation_session_local_registry(session),
      ag_compilation_session_lowering_context(session),
      ag_compilation_session_options_view(session),
      syntax_expression, fallback_diag_tok);
}

node_t *psx_frontend_analyze_initializer_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax, const token_t *fallback_diag_tok) {
  if (!semantic_context || !syntax) return NULL;
  node_t *initializer = psx_clone_syntax_tree_for_resolution(
      ps_ctx_arena(semantic_context), syntax);
  if (!initializer) {
    ag_diagnostic_context_t *diagnostics =
        ps_ctx_diagnostics(semantic_context);
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: could not create initializer resolver working tree",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED));
    return NULL;
  }
  initializer = psx_bind_identifier_initializer_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      initializer, fallback_diag_tok);
  psx_semantic_resolve_initializer_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      initializer, NULL, fallback_diag_tok);
  psx_require_available_semantic_tree_types_interned(
      semantic_context, ps_ctx_diagnostics(semantic_context), initializer,
      fallback_diag_tok);
  initializer = psx_lower_semantic_initializer_syntax_in_contexts(
      semantic_context, global_registry, local_registry,
      lowering_context, options,
      initializer, fallback_diag_tok);
  psx_semantic_resolve_initializer_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      initializer, NULL, fallback_diag_tok);
  psx_require_semantic_initializer_has_interned_expression_types(
      semantic_context, ps_ctx_diagnostics(semantic_context), initializer,
      fallback_diag_tok);
  psx_require_semantic_initializer_has_canonical_expression_types(
      ps_ctx_diagnostics(semantic_context), initializer, fallback_diag_tok);
  return initializer;
}

void psx_frontend_analyze_program_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    node_t **program) {
  if (!program) return;
  for (int i = 0; program[i]; i++) {
    if (program[i]->kind == ND_FUNCDEF) continue;
    program[i] = psx_bind_identifier_tree_in_contexts(
        semantic_context, global_registry, local_registry,
        program[i], program[i]->tok);
    psx_semantic_resolve_tree_in_contexts(
        semantic_context, global_registry, local_registry,
        program[i], NULL, program[i]->tok);
    psx_require_available_semantic_tree_types_interned(
        semantic_context, ps_ctx_diagnostics(semantic_context), program[i],
        program[i]->tok);
    program[i] = psx_lower_semantic_tree_in_contexts(
        semantic_context, global_registry, local_registry,
        lowering_context, options,
        program[i], program[i]->tok);
    psx_semantic_resolve_tree_in_contexts(
        semantic_context, global_registry, local_registry,
        program[i], NULL, program[i]->tok);
    psx_lower_implicit_conversions(
        lowering_context, program[i], NULL, program[i]->tok, options);
    psx_require_semantic_tree_has_interned_expression_types(
        semantic_context, ps_ctx_diagnostics(semantic_context), program[i],
        program[i]->tok);
    psx_require_semantic_tree_has_canonical_expression_types(
        ps_ctx_diagnostics(semantic_context), program[i], program[i]->tok);
  }
}

void psx_frontend_analyze_program_in_session(
    ag_compilation_session_t *session, node_t **program) {
  if (!ag_compilation_session_is_complete(session)) return;
  psx_frontend_analyze_program_in_contexts(
      ag_compilation_session_semantic_context(session),
      ag_compilation_session_global_registry(session),
      ag_compilation_session_local_registry(session),
      ag_compilation_session_lowering_context(session),
      ag_compilation_session_options_view(session), program);
}
