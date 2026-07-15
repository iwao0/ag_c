#include "semantic_pipeline.h"

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

static void analyze_function_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const ag_compilation_options_t *options,
    node_t *function, const token_t *fallback_diag_tok) {
  if (!semantic_context || !local_registry ||
      !function || function->kind != ND_FUNCDEF) return;
  function = psx_bind_identifier_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      function, fallback_diag_tok);
  node_function_definition_t *current_function =
      (node_function_definition_t *)function;
  psx_validate_control_flow(function, fallback_diag_tok);
  psx_semantic_resolve_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      function, current_function, fallback_diag_tok);
  function = psx_lower_semantic_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      options,
      function, fallback_diag_tok);
  psx_semantic_resolve_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      function, current_function, fallback_diag_tok);
  current_function->lvars = ps_decl_get_locals_in(local_registry);
  psx_emit_semantic_warnings(
      function, current_function, fallback_diag_tok);
  psx_emit_unreachable_warnings(function, fallback_diag_tok);
  psx_lower_implicit_conversions(
      function, current_function, fallback_diag_tok, options);
  psx_require_semantic_tree_has_canonical_expression_types(
      function, fallback_diag_tok);
  psx_analyze_function_lvar_usage_in(
      local_registry, current_function, fallback_diag_tok);
}

void psx_frontend_analyze_function_in_session(
    ag_compilation_session_t *session,
    node_t *function, const token_t *fallback_diag_tok) {
  if (!ag_compilation_session_is_complete(session)) return;
  analyze_function_in_contexts(
      ag_compilation_session_semantic_context(session),
      ag_compilation_session_global_registry(session),
      ag_compilation_session_local_registry(session),
      ag_compilation_session_options_view(session),
      function, fallback_diag_tok);
}

node_t *psx_frontend_analyze_expression_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const ag_compilation_options_t *options,
    node_t *expression, const token_t *fallback_diag_tok) {
  expression = psx_bind_identifier_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      expression, fallback_diag_tok);
  psx_semantic_resolve_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      expression, NULL, fallback_diag_tok);
  expression = psx_lower_semantic_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      options,
      expression, fallback_diag_tok);
  psx_semantic_resolve_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      expression, NULL, fallback_diag_tok);
  psx_lower_implicit_conversions(
      expression, NULL, fallback_diag_tok, options);
  psx_require_semantic_tree_has_canonical_expression_types(
      expression, fallback_diag_tok);
  return expression;
}

node_t *psx_frontend_analyze_expression_in_session(
    ag_compilation_session_t *session,
    node_t *expression, const token_t *fallback_diag_tok) {
  if (!ag_compilation_session_is_complete(session)) return expression;
  return psx_frontend_analyze_expression_in_contexts(
      ag_compilation_session_semantic_context(session),
      ag_compilation_session_global_registry(session),
      ag_compilation_session_local_registry(session),
      ag_compilation_session_options_view(session),
      expression, fallback_diag_tok);
}

node_t *psx_frontend_analyze_initializer_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const ag_compilation_options_t *options,
    node_t *syntax, const token_t *fallback_diag_tok) {
  syntax = psx_bind_identifier_initializer_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      syntax, fallback_diag_tok);
  psx_semantic_resolve_initializer_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      syntax, NULL, fallback_diag_tok);
  syntax = psx_lower_semantic_initializer_syntax_in_contexts(
      semantic_context, global_registry, local_registry,
      options,
      syntax, fallback_diag_tok);
  psx_semantic_resolve_initializer_tree_in_contexts(
      semantic_context, global_registry, local_registry,
      syntax, NULL, fallback_diag_tok);
  psx_require_semantic_initializer_has_canonical_expression_types(
      syntax, fallback_diag_tok);
  return syntax;
}

void psx_frontend_analyze_program_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
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
    program[i] = psx_lower_semantic_tree_in_contexts(
        semantic_context, global_registry, local_registry,
        options,
        program[i], program[i]->tok);
    psx_semantic_resolve_tree_in_contexts(
        semantic_context, global_registry, local_registry,
        program[i], NULL, program[i]->tok);
    psx_lower_implicit_conversions(
        program[i], NULL, program[i]->tok, options);
    psx_require_semantic_tree_has_canonical_expression_types(
        program[i], program[i]->tok);
  }
}

void psx_frontend_analyze_program_in_session(
    ag_compilation_session_t *session, node_t **program) {
  if (!ag_compilation_session_is_complete(session)) return;
  psx_frontend_analyze_program_in_contexts(
      ag_compilation_session_semantic_context(session),
      ag_compilation_session_global_registry(session),
      ag_compilation_session_local_registry(session),
      ag_compilation_session_options_view(session), program);
}
