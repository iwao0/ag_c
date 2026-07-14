#include "semantic_pipeline.h"

#include "../lowering/semantic_lowering_pass.h"
#include "../parser/decl.h"
#include "../parser/semantic_ctx.h"
#include "../semantic/control_flow_validation.h"
#include "../semantic/identifier_binding.h"
#include "../semantic/lvar_usage_analysis.h"
#include "../semantic/semantic_diagnostics.h"
#include "../semantic/semantic_pass.h"

static void analyze_function_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    node_t *function, const token_t *fallback_diag_tok) {
  if (!semantic_context || !local_registry ||
      !function || function->kind != ND_FUNCDEF) return;
  function = psx_bind_identifier_tree_in_contexts(
      semantic_context, local_registry,
      function, fallback_diag_tok);
  node_function_definition_t *current_function =
      (node_function_definition_t *)function;
  psx_validate_control_flow(function, fallback_diag_tok);
  psx_semantic_resolve_tree_in_contexts(
      semantic_context, local_registry,
      function, current_function, fallback_diag_tok);
  function = psx_lower_semantic_tree_in(
      local_registry, function, fallback_diag_tok);
  psx_semantic_resolve_tree_in_contexts(
      semantic_context, local_registry,
      function, current_function, fallback_diag_tok);
  current_function->lvars = ps_decl_get_locals_in(local_registry);
  psx_emit_semantic_warnings(
      function, current_function, fallback_diag_tok);
  psx_emit_unreachable_warnings(function, fallback_diag_tok);
  psx_lower_implicit_conversions(
      function, current_function, fallback_diag_tok);
  psx_analyze_function_lvar_usage_in(
      local_registry, current_function, fallback_diag_tok);
}

void psx_frontend_analyze_function_in_context(
    psx_semantic_context_t *semantic_context,
    node_t *function, const token_t *fallback_diag_tok) {
  analyze_function_in_contexts(
      semantic_context ? semantic_context : ps_ctx_active(),
      ps_local_registry_active(), function, fallback_diag_tok);
}

void psx_frontend_analyze_function_in_compiler_context(
    ag_compiler_context_t *compiler_context,
    node_t *function, const token_t *fallback_diag_tok) {
  if (!compiler_context) {
    psx_frontend_analyze_function_in_context(
        NULL, function, fallback_diag_tok);
    return;
  }
  analyze_function_in_contexts(
      compiler_context->semantic_context,
      compiler_context->local_registry,
      function, fallback_diag_tok);
}

void psx_frontend_analyze_function(
    node_t *function, const token_t *fallback_diag_tok) {
  psx_frontend_analyze_function_in_context(
      ps_ctx_active(), function, fallback_diag_tok);
}

node_t *psx_frontend_analyze_expression_in_context(
    psx_semantic_context_t *semantic_context,
    node_t *expression, const token_t *fallback_diag_tok) {
  return psx_frontend_analyze_expression_in_contexts(
      semantic_context, ps_local_registry_active(),
      expression, fallback_diag_tok);
}

node_t *psx_frontend_analyze_expression_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    node_t *expression, const token_t *fallback_diag_tok) {
  expression = psx_bind_identifier_tree_in_contexts(
      semantic_context, local_registry,
      expression, fallback_diag_tok);
  psx_semantic_resolve_tree_in_contexts(
      semantic_context, local_registry,
      expression, NULL, fallback_diag_tok);
  expression = psx_lower_semantic_tree_in(
      local_registry, expression, fallback_diag_tok);
  psx_semantic_resolve_tree_in_contexts(
      semantic_context, local_registry,
      expression, NULL, fallback_diag_tok);
  psx_lower_implicit_conversions(expression, NULL, fallback_diag_tok);
  return expression;
}

node_t *psx_frontend_analyze_expression(
    node_t *expression, const token_t *fallback_diag_tok) {
  return psx_frontend_analyze_expression_in_context(
      ps_ctx_active(), expression, fallback_diag_tok);
}

node_t *psx_frontend_analyze_initializer_syntax_in_context(
    psx_semantic_context_t *semantic_context,
    node_t *syntax, const token_t *fallback_diag_tok) {
  return psx_frontend_analyze_initializer_syntax_in_contexts(
      semantic_context, ps_local_registry_active(),
      syntax, fallback_diag_tok);
}

node_t *psx_frontend_analyze_initializer_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    node_t *syntax, const token_t *fallback_diag_tok) {
  syntax = psx_bind_identifier_initializer_tree_in_contexts(
      semantic_context, local_registry,
      syntax, fallback_diag_tok);
  psx_semantic_resolve_initializer_tree_in_contexts(
      semantic_context, local_registry,
      syntax, NULL, fallback_diag_tok);
  syntax = psx_lower_semantic_initializer_syntax_in(
      local_registry, syntax, fallback_diag_tok);
  psx_semantic_resolve_initializer_tree_in_contexts(
      semantic_context, local_registry,
      syntax, NULL, fallback_diag_tok);
  return syntax;
}

node_t *psx_frontend_analyze_initializer_syntax(
    node_t *syntax, const token_t *fallback_diag_tok) {
  return psx_frontend_analyze_initializer_syntax_in_context(
      ps_ctx_active(), syntax, fallback_diag_tok);
}

void psx_frontend_analyze_program_in_context(
    psx_semantic_context_t *semantic_context, node_t **program) {
  if (!program) return;
  for (int i = 0; program[i]; i++) {
    if (program[i]->kind == ND_FUNCDEF) continue;
    program[i] = psx_bind_identifier_tree_in(
        semantic_context,
        program[i], program[i]->tok);
    psx_semantic_resolve_tree_in_context(
        semantic_context, program[i], NULL, program[i]->tok);
    program[i] = psx_lower_semantic_tree(program[i], program[i]->tok);
    psx_semantic_resolve_tree_in_context(
        semantic_context, program[i], NULL, program[i]->tok);
    psx_lower_implicit_conversions(program[i], NULL, program[i]->tok);
  }
}

void psx_frontend_analyze_program(node_t **program) {
  psx_frontend_analyze_program_in_context(
      ps_ctx_active(), program);
}
