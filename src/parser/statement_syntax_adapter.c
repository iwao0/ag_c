#include "expr.h"
#include "runtime_context.h"
#include "statement_syntax_adapter.h"
#include "stmt.h"

static node_t *parse_expression(void *context) {
  psx_statement_syntax_adapter_t *adapter = context;
  return psx_expr_expr_with_syntax_services(
      adapter->runtime_context, &adapter->name_classifier,
      adapter->local_declarations);
}

static node_t *parse_local_declaration(void *context) {
  psx_statement_syntax_adapter_t *adapter = context;
  return psx_parse_local_declaration_syntax(
      adapter->local_declarations);
}

static node_t *parse_case_expression(void *context) {
  psx_statement_syntax_adapter_t *adapter = context;
  return psx_expr_conditional_with_syntax_services(
      adapter->runtime_context, &adapter->name_classifier,
      adapter->local_declarations);
}

static void enter_block_scope(void *context) {
  psx_statement_syntax_adapter_t *adapter = context;
  ps_name_classifier_enter_scope(&adapter->name_classifier);
}

static void leave_block_scope(void *context) {
  psx_statement_syntax_adapter_t *adapter = context;
  ps_name_classifier_leave_scope(&adapter->name_classifier);
}

static void enter_local_scope(void *context) {
  psx_statement_syntax_adapter_t *adapter = context;
  ps_name_classifier_enter_scope(&adapter->name_classifier);
}

static void leave_local_scope(void *context) {
  psx_statement_syntax_adapter_t *adapter = context;
  ps_name_classifier_leave_scope(&adapter->name_classifier);
}

psx_statement_syntax_context_t psx_statement_syntax_adapter_context(
    psx_statement_syntax_adapter_t *adapter) {
  return (psx_statement_syntax_context_t){
      .context = adapter,
      .runtime_context = adapter->runtime_context,
      .name_classifier = adapter->name_classifier,
      .parse_expression = parse_expression,
      .parse_local_declaration = parse_local_declaration,
      .parse_case_expression = parse_case_expression,
      .enter_block_scope = enter_block_scope,
      .leave_block_scope = leave_block_scope,
      .enter_local_scope = enter_local_scope,
      .leave_local_scope = leave_local_scope,
  };
}

int psx_statement_syntax_adapter_init(
    psx_statement_syntax_adapter_t *adapter,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations) {
  if (!adapter || !runtime_context) return 0;
  *adapter = (psx_statement_syntax_adapter_t){
      .runtime_context = runtime_context,
      .local_declarations = local_declarations,
      .name_classifier =
          name_classifier ? *name_classifier : (psx_name_classifier_t){0},
  };
  return 1;
}
