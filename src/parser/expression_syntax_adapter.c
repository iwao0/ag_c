#include "expr.h"

#include "decl.h"
#include "declaration_syntax.h"
#include "initializer_syntax.h"
#include "local_registry.h"
#include "runtime_context.h"
#include "semantic_ctx.h"
#include "stmt.h"
#include "../diag/diag.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_parser_runtime_context_t *runtime_context;
  const psx_local_declaration_callbacks_t *local_declarations;
  psx_name_classifier_t name_classifier;
} psx_legacy_expression_syntax_adapter_t;

static void capture_lookup_point(
    void *context, unsigned *scope_seq,
    unsigned *declaration_seq) {
  psx_legacy_expression_syntax_adapter_t *adapter = context;
  psx_local_lookup_point_t point =
      ps_local_registry_capture_lookup_point_in(
          adapter->local_registry);
  if (scope_seq) *scope_seq = point.scope_seq;
  if (declaration_seq) *declaration_seq = point.declaration_seq;
}

static void current_function_name(
    void *context, char **name, int *name_len) {
  psx_legacy_expression_syntax_adapter_t *adapter = context;
  ps_decl_get_current_funcname_in(
      adapter->local_registry, name, name_len);
}

static node_t *parse_initializer_list(void *context) {
  psx_legacy_expression_syntax_adapter_t *adapter = context;
  return psx_parse_initializer_syntax_list_in_contexts(
      adapter->semantic_context, adapter->global_registry,
      adapter->local_registry, adapter->runtime_context,
      &adapter->name_classifier, adapter->local_declarations);
}

static node_t *parse_statement_expression(void *context) {
  psx_legacy_expression_syntax_adapter_t *adapter = context;
  return psx_parse_statement_expression_in_contexts(
      adapter->semantic_context, adapter->global_registry,
      adapter->local_registry, adapter->runtime_context,
      &adapter->name_classifier, adapter->local_declarations);
}

static void diagnose_type_name_complex_requires_float(
    void *context, token_t *token) {
  psx_legacy_expression_syntax_adapter_t *adapter = context;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(adapter->semantic_context);
  diag_emit_tokf_in(
      diagnostics, DIAG_ERR_PARSER_INVALID_CONTEXT, token, "%s",
      diag_message_for_in(
          diagnostics,
          DIAG_ERR_PARSER_COMPLEX_IMAGINARY_CAST_REQUIRES_FLOAT));
}

static int parse_type_name(
    void *context, token_t *start, int runtime_bounds,
    psx_parsed_type_name_t *out) {
  psx_legacy_expression_syntax_adapter_t *adapter = context;
  psx_decl_specifier_syntax_options_t options = {
      .name_classifier = &adapter->name_classifier,
      .diagnose_complex_requires_float =
          diagnose_type_name_complex_requires_float,
      .context = adapter,
      .semantic_context = adapter->semantic_context,
      .global_registry = adapter->global_registry,
      .local_registry = adapter->local_registry,
      .runtime_context = adapter->runtime_context,
  };
  int parsed = runtime_bounds
                   ? psx_parse_runtime_type_name_syntax_at(
                         start, &options, out)
                   : psx_parse_type_name_syntax_at(
                         start, &options, out);
  if (parsed && runtime_bounds)
    ps_parse_runtime_declarator_expressions_in_contexts(
        &out->declarator, adapter->semantic_context,
        adapter->global_registry, adapter->local_registry,
        adapter->runtime_context, adapter->local_declarations);
  return parsed;
}

static psx_expression_syntax_context_t expression_syntax_context(
    psx_legacy_expression_syntax_adapter_t *adapter) {
  return (psx_expression_syntax_context_t){
      .context = adapter,
      .runtime_context = adapter->runtime_context,
      .name_classifier = adapter->name_classifier,
      .capture_lookup_point = capture_lookup_point,
      .current_function_name = current_function_name,
      .parse_initializer_list = parse_initializer_list,
      .parse_statement_expression = parse_statement_expression,
      .parse_type_name = parse_type_name,
  };
}

node_t *psx_expr_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations) {
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context)
    return NULL;
  psx_legacy_expression_syntax_adapter_t adapter = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .runtime_context = runtime_context,
      .local_declarations = local_declarations,
      .name_classifier =
          name_classifier ? *name_classifier : (psx_name_classifier_t){0},
  };
  psx_expression_syntax_context_t syntax_context =
      expression_syntax_context(&adapter);
  return psx_expr_expr_syntax(&syntax_context);
}

node_t *psx_expr_assign_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations) {
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context)
    return NULL;
  psx_legacy_expression_syntax_adapter_t adapter = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .runtime_context = runtime_context,
      .local_declarations = local_declarations,
      .name_classifier =
          name_classifier ? *name_classifier : (psx_name_classifier_t){0},
  };
  psx_expression_syntax_context_t syntax_context =
      expression_syntax_context(&adapter);
  return psx_expr_assign_syntax(&syntax_context);
}
