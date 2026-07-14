#include "local_declaration_syntax.h"

#include "config_runtime.h"
#include "diag.h"
#include "node_utils.h"
#include "parser_recovery.h"
#include "semantic_ctx.h"
#include "static_assert_declaration.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../tokenizer/tokenizer.h"

static token_t *curtok(void) { return tk_get_current_token(); }

static int is_local_typedef_name(token_t *token, void *context) {
  return psx_ctx_is_typedef_name_token_in(context, token);
}

static void require_callbacks(
    const psx_local_declaration_callbacks_t *callbacks) {
  if (!callbacks || !callbacks->apply_static_assert ||
      !callbacks->begin_declaration || !callbacks->begin_declarator ||
      !callbacks->finish_declarator || !callbacks->finish_declaration) {
    ps_diag_ctx(curtok(), "local-declaration-syntax",
                "local declaration application callbacks are required");
  }
}

node_t *psx_parse_local_declaration_syntax(
    const psx_local_declaration_callbacks_t *callbacks) {
  require_callbacks(callbacks);
  if (curtok()->kind == TK_STATIC_ASSERT) {
    psx_parsed_static_assert_declaration_t assertion;
    psx_parse_static_assert_syntax_in_context(
        &assertion, callbacks->context, callbacks);
    callbacks->apply_static_assert(
        callbacks->context, assertion.condition,
        assertion.diagnostic_token);
    return ps_node_new_num(0);
  }

  int is_typedef = curtok()->kind == TK_TYPEDEF;
  if (is_typedef) tk_set_current_token(curtok()->next);
  psx_parsed_decl_specifier_t specifier;
  if (!psx_try_parse_decl_specifier_syntax_ex(
          &specifier,
          &(psx_decl_specifier_syntax_options_t){
              .is_typedef_name = is_local_typedef_name,
              .context = callbacks->context,
              .semantic_context = callbacks->context,
          })) {
    diag_report_tokf(
        DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN, curtok(), "%s",
        diag_message_for(DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN));
    ps_parser_mark_recoverable_syntax_error();
    return NULL;
  }
  ps_prepare_decl_specifier_alignments_in_context(
      &specifier, callbacks->context);
  int standalone_tag =
      specifier.source == PSX_PARSED_DECL_TYPE_TAG &&
      curtok()->kind == TK_SEMI;
  void *declaration_context = callbacks->begin_declaration(
      callbacks->context, &specifier, is_typedef, standalone_tag);

  if (standalone_tag) {
    tk_expect(';');
    node_t *result =
        callbacks->finish_declaration(declaration_context);
    ps_dispose_decl_specifier_syntax(&specifier);
    return result;
  }

  int declarator_count = 0;
  for (;;) {
    if (++declarator_count > PS_MAX_DECLARATOR_COUNT) {
      ps_diag_ctx(curtok(), "decl",
                  diag_message_for(
                      DIAG_ERR_PARSER_DECLARATOR_LIST_TOO_LONG),
                  PS_MAX_DECLARATOR_COUNT);
    }
    psx_parsed_declarator_t declarator;
    psx_parse_declarator_syntax_tree_into_with_typedef_lookup(
        &declarator, is_local_typedef_name, callbacks->context);
    ps_parse_runtime_declarator_expressions_in_context(
        &declarator, callbacks->context, callbacks);
    if (!declarator.identifier) {
      ps_diag_ctx(curtok(), "decl", "%s",
                  diag_message_for(
                      DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
    }

    psx_parsed_initializer_t initializer;
    psx_prepare_optional_initializer_syntax(&initializer);
    if (initializer.has_initializer && initializer.value_tok &&
        (initializer.value_tok->kind == TK_SEMI ||
         initializer.value_tok->kind == TK_COMMA ||
         initializer.value_tok->kind == TK_RBRACE ||
         initializer.value_tok->kind == TK_EOF)) {
      diag_report_tokf(
          DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED, initializer.value_tok,
          "%s", diag_message_for(DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED));
      psx_dispose_declarator_syntax(&declarator);
      if (callbacks->abort_declaration)
        callbacks->abort_declaration(declaration_context);
      else
        callbacks->finish_declaration(declaration_context);
      ps_dispose_decl_specifier_syntax(&specifier);
      ps_parser_mark_recoverable_syntax_error();
      return NULL;
    }
    callbacks->begin_declarator(
        declaration_context, &declarator, &initializer);
    if (initializer.has_initializer) {
      token_t *assign_tok = initializer.assign_tok;
      tk_expect('=');
      psx_parse_initializer_syntax_value_in_context(
          &initializer, assign_tok, callbacks->context, callbacks);
    }
    callbacks->finish_declarator(
        declaration_context, &initializer);
    psx_dispose_declarator_syntax(&declarator);
    if (!tk_consume(',')) break;
  }
  tk_expect(';');
  node_t *result = callbacks->finish_declaration(declaration_context);
  ps_dispose_decl_specifier_syntax(&specifier);
  return result;
}
