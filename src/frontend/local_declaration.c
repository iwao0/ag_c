#include "local_declaration.h"

#include "../diag/diag.h"
#include "../parser/expr.h"
#include "../parser/node_utils.h"
#include "../parser/runtime_context.h"
#include "../parser/static_assert_declaration.h"

static node_t *parse_local_initializer_assignment_expression(
    void *context);

static node_t *parse_local_static_assert_syntax(void *context) {
  psx_frontend_local_declaration_syntax_adapter_t *adapter = context;
  if (!adapter || !adapter->syntax) return NULL;
  psx_parsed_static_assert_declaration_t assertion;
  psx_parse_static_assert_syntax_with_context(
      &assertion,
      &(psx_static_assert_syntax_context_t){
          .context = adapter,
          .runtime_context = adapter->runtime_context,
          .parse_assignment_expression =
              parse_local_initializer_assignment_expression,
      });
  return psx_node_new_static_assert_syntax_in(
      ps_parser_runtime_arena(adapter->runtime_context),
      assertion.condition, assertion.diagnostic_token);
}

static int parse_local_decl_specifier_syntax(
    void *context, psx_parsed_decl_specifier_t *specifier) {
  psx_frontend_local_declaration_syntax_adapter_t *adapter = context;
  if (!adapter || !adapter->syntax || !specifier) return 0;
  return psx_try_parse_decl_specifier_syntax_ex(
      specifier,
      &(psx_decl_specifier_syntax_options_t){
          .name_classifier = &adapter->syntax->name_classifier,
          .expression_context = adapter,
          .parse_assignment_expression =
              parse_local_initializer_assignment_expression,
          .runtime_context = adapter->runtime_context,
      });
}

static void parse_local_declarator_syntax(
    void *context, psx_parsed_declarator_t *declarator) {
  psx_frontend_local_declaration_syntax_adapter_t *adapter = context;
  if (!adapter || !adapter->syntax || !declarator) return;
  psx_parse_declarator_syntax_tree_into_with_typedef_lookup_in_contexts(
      declarator,
      &(psx_decl_specifier_syntax_options_t){
          .name_classifier = &adapter->syntax->name_classifier,
          .expression_context = adapter,
          .parse_assignment_expression =
              parse_local_initializer_assignment_expression,
          .runtime_context = adapter->runtime_context,
      });
}

static node_t *parse_local_initializer_assignment_expression(
    void *context) {
  psx_frontend_local_declaration_syntax_adapter_t *adapter = context;
  if (!adapter || !adapter->syntax) return NULL;
  return psx_expr_assign_with_syntax_services(
      adapter->runtime_context, &adapter->syntax->name_classifier,
      adapter->syntax);
}

static void diagnose_local_initializer_unsupported_gnu_extension(
    void *context, const token_t *token, const char *name) {
  psx_frontend_local_declaration_syntax_adapter_t *adapter = context;
  if (!adapter) return;
  ag_diagnostic_context_t *diagnostics =
      ps_parser_runtime_diagnostics(adapter->runtime_context);
  diag_emit_tokf_in(
      diagnostics, DIAG_ERR_PARSER_UNSUPPORTED_GNU_EXTENSION,
      token,
      diag_message_for_in(
          diagnostics, DIAG_ERR_PARSER_UNSUPPORTED_GNU_EXTENSION),
      name ? name : "");
}

static void parse_local_initializer_syntax(
    void *context, psx_parsed_initializer_t *initializer,
    token_t *assign_tok) {
  psx_frontend_local_declaration_syntax_adapter_t *adapter = context;
  if (!adapter || !adapter->syntax || !initializer) return;
  psx_parse_initializer_syntax_value_with_context(
      initializer, assign_tok,
      &(psx_initializer_syntax_context_t){
          .context = adapter,
          .runtime_context = adapter->runtime_context,
          .parse_assignment_expression =
              parse_local_initializer_assignment_expression,
          .diagnose_unsupported_gnu_extension =
              diagnose_local_initializer_unsupported_gnu_extension,
      });
}

void psx_frontend_init_local_declaration_syntax_adapter(
    psx_frontend_local_declaration_syntax_adapter_t *adapter,
    psx_local_declaration_callbacks_t *callbacks,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier) {
  if (!adapter || !callbacks) return;
  *adapter = (psx_frontend_local_declaration_syntax_adapter_t){0};
  *callbacks = (psx_local_declaration_callbacks_t){0};
  if (!runtime_context) return;
  *adapter = (psx_frontend_local_declaration_syntax_adapter_t){
      .runtime_context = runtime_context,
      .syntax = callbacks,
  };
  *callbacks = (psx_local_declaration_callbacks_t){
      .context = adapter,
      .name_classifier = name_classifier
                             ? *name_classifier
                             : (psx_name_classifier_t){0},
      .runtime_context = runtime_context,
      .parse_static_assert = parse_local_static_assert_syntax,
      .parse_decl_specifier = parse_local_decl_specifier_syntax,
      .parse_declarator = parse_local_declarator_syntax,
      .parse_initializer = parse_local_initializer_syntax,
  };
}
