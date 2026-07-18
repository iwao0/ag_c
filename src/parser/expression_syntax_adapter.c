#include "expr.h"

#include "declaration_syntax.h"
#include "initializer_syntax.h"
#include "runtime_context.h"
#include "statement_syntax_adapter.h"
#include "stmt.h"
#include "../diag/diag.h"

#include <stddef.h>

typedef struct {
  psx_parser_runtime_context_t *runtime_context;
  const psx_local_declaration_callbacks_t *local_declarations;
  psx_name_classifier_t name_classifier;
  char *current_function_name;
  int current_function_name_len;
} psx_expression_syntax_adapter_t;

static psx_expression_syntax_context_t expression_syntax_context(
    psx_expression_syntax_adapter_t *adapter);

static void capture_lookup_point(
    void *context, unsigned *scope_seq,
    unsigned *declaration_seq) {
  psx_expression_syntax_adapter_t *adapter = context;
  if (ps_name_classifier_capture_lookup_point(
          &adapter->name_classifier,
          scope_seq, declaration_seq))
    return;
  if (scope_seq) *scope_seq = 0;
  if (declaration_seq) *declaration_seq = 0;
}

static void current_function_name(
    void *context, char **name, int *name_len) {
  psx_expression_syntax_adapter_t *adapter = context;
  if (name) *name = adapter->current_function_name;
  if (name_len) *name_len = adapter->current_function_name_len;
}

static int nested_is_typedef_name(
    void *context, const token_t *token) {
  psx_expression_syntax_adapter_t *adapter = context;
  return ps_name_classifier_is_typedef_name(
      &adapter->name_classifier, token);
}

static void nested_declare_name(
    void *context, const token_t *token, int is_typedef_name) {
  psx_expression_syntax_adapter_t *adapter = context;
  ps_name_classifier_declare(
      &adapter->name_classifier, token, is_typedef_name);
}

static void nested_enter_scope(void *context) {
  psx_expression_syntax_adapter_t *adapter = context;
  ps_name_classifier_enter_scope(&adapter->name_classifier);
}

static void nested_leave_scope(void *context) {
  psx_expression_syntax_adapter_t *adapter = context;
  ps_name_classifier_leave_scope(&adapter->name_classifier);
}

static void nested_record_binding_event(void *context) {
  psx_expression_syntax_adapter_t *adapter = context;
  ps_name_classifier_record_binding_event(
      &adapter->name_classifier);
}

static void nested_reserve_scope(void *context) {
  psx_expression_syntax_adapter_t *adapter = context;
  ps_name_classifier_reserve_scope(&adapter->name_classifier);
}

static void nested_capture_lookup_point(
    void *context, unsigned *scope_seq,
    unsigned *declaration_seq) {
  capture_lookup_point(context, scope_seq, declaration_seq);
}

static node_t *parse_initializer_assignment_expression(
    void *context) {
  psx_expression_syntax_adapter_t *adapter = context;
  psx_expression_syntax_context_t syntax_context =
      expression_syntax_context(adapter);
  return psx_expr_assign_syntax(&syntax_context);
}

static void diagnose_initializer_unsupported_gnu_extension(
    void *context, const token_t *token, const char *name) {
  psx_expression_syntax_adapter_t *adapter = context;
  ag_diagnostic_context_t *diagnostics =
      ps_parser_runtime_diagnostics(adapter->runtime_context);
  diag_emit_tokf_in(
      diagnostics, DIAG_ERR_PARSER_UNSUPPORTED_GNU_EXTENSION,
      token,
      diag_message_for_in(
          diagnostics, DIAG_ERR_PARSER_UNSUPPORTED_GNU_EXTENSION),
      name ? name : "");
}

static node_t *parse_initializer_list(void *context) {
  psx_expression_syntax_adapter_t *adapter = context;
  return psx_parse_initializer_syntax_list_with_context(
      &(psx_initializer_syntax_context_t){
          .context = adapter,
          .runtime_context = adapter->runtime_context,
          .parse_assignment_expression =
              parse_initializer_assignment_expression,
          .diagnose_unsupported_gnu_extension =
              diagnose_initializer_unsupported_gnu_extension,
      });
}

static node_t *parse_statement_expression(void *context) {
  psx_expression_syntax_adapter_t *adapter = context;
  psx_name_classifier_t nested_classifier = {
      .context = adapter,
      .is_typedef_name = nested_is_typedef_name,
      .declare_name = nested_declare_name,
      .enter_scope = nested_enter_scope,
      .leave_scope = nested_leave_scope,
      .record_binding_event = nested_record_binding_event,
      .reserve_scope = nested_reserve_scope,
      .capture_lookup_point = nested_capture_lookup_point,
  };
  psx_statement_syntax_adapter_t statement_adapter;
  if (!psx_statement_syntax_adapter_init(
          &statement_adapter, adapter->runtime_context,
          &nested_classifier, adapter->local_declarations,
          adapter->current_function_name,
          adapter->current_function_name_len))
    return NULL;
  psx_statement_syntax_context_t syntax =
      psx_statement_syntax_adapter_context(&statement_adapter);
  return psx_parse_statement_expression_syntax(&syntax);
}

static void diagnose_type_name_complex_requires_float(
    void *context, token_t *token) {
  psx_expression_syntax_adapter_t *adapter = context;
  ag_diagnostic_context_t *diagnostics =
      ps_parser_runtime_diagnostics(adapter->runtime_context);
  diag_emit_tokf_in(
      diagnostics, DIAG_ERR_PARSER_INVALID_CONTEXT, token, "%s",
      diag_message_for_in(
          diagnostics,
          DIAG_ERR_PARSER_COMPLEX_IMAGINARY_CAST_REQUIRES_FLOAT));
}

static int parse_type_name(
    void *context, token_t *start, int runtime_bounds,
    psx_parsed_type_name_t *out) {
  psx_expression_syntax_adapter_t *adapter = context;
  psx_decl_specifier_syntax_options_t options = {
      .name_classifier = &adapter->name_classifier,
      .diagnose_complex_requires_float =
          diagnose_type_name_complex_requires_float,
      .context = adapter,
      .expression_context = adapter,
      .parse_assignment_expression =
          parse_initializer_assignment_expression,
      .runtime_context = adapter->runtime_context,
  };
  int parsed = runtime_bounds
                   ? psx_parse_runtime_type_name_syntax_at(
                         start, &options, out)
                   : psx_parse_type_name_syntax_at(
                         start, &options, out);
  return parsed;
}

static psx_expression_syntax_context_t expression_syntax_context(
    psx_expression_syntax_adapter_t *adapter) {
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

static node_t *parse_with_syntax_services(
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations,
    char *current_function_name, int current_function_name_len,
    node_t *(*parse)(const psx_expression_syntax_context_t *)) {
  if (!runtime_context || !parse) return NULL;
  psx_expression_syntax_adapter_t adapter = {
      .runtime_context = runtime_context,
      .local_declarations = local_declarations,
      .name_classifier =
          name_classifier ? *name_classifier : (psx_name_classifier_t){0},
      .current_function_name = current_function_name,
      .current_function_name_len = current_function_name_len,
  };
  psx_expression_syntax_context_t syntax_context =
      expression_syntax_context(&adapter);
  return parse(&syntax_context);
}

node_t *psx_expr_expr_with_syntax_services(
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations,
    char *current_function_name, int current_function_name_len) {
  return parse_with_syntax_services(
      runtime_context, name_classifier, local_declarations,
      current_function_name, current_function_name_len,
      psx_expr_expr_syntax);
}

node_t *psx_expr_assign_with_syntax_services(
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations,
    char *current_function_name, int current_function_name_len) {
  return parse_with_syntax_services(
      runtime_context, name_classifier, local_declarations,
      current_function_name, current_function_name_len,
      psx_expr_assign_syntax);
}

node_t *psx_expr_conditional_with_syntax_services(
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations,
    char *current_function_name, int current_function_name_len) {
  return parse_with_syntax_services(
      runtime_context, name_classifier, local_declarations,
      current_function_name, current_function_name_len,
      psx_expr_conditional_syntax);
}
