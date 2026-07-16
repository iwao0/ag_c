#include "local_declaration_syntax.h"

#include "diag.h"
#include "node_utils.h"
#include "parser_recovery.h"
#include "runtime_context.h"
#include "semantic_ctx.h"
#include "static_assert_declaration.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../tokenizer/tokenizer.h"

static tokenizer_context_t *tokenizer(
    const psx_local_declaration_callbacks_t *callbacks) {
  return callbacks && callbacks->runtime_context
             ? ps_parser_runtime_tokenizer(callbacks->runtime_context)
             : NULL;
}

static token_t *curtok(
    const psx_local_declaration_callbacks_t *callbacks) {
  return tk_get_current_token_ctx(tokenizer(callbacks));
}

static ag_diagnostic_context_t *diagnostics(
    const psx_local_declaration_callbacks_t *callbacks) {
  return callbacks && callbacks->runtime_context
             ? ps_parser_runtime_diagnostics(callbacks->runtime_context)
             : NULL;
}

static int callbacks_are_complete(
    const psx_local_declaration_callbacks_t *callbacks) {
  if (!callbacks ||
      !callbacks->begin_declaration || !callbacks->begin_declarator ||
      !callbacks->finish_declarator || !callbacks->finish_declaration ||
      !callbacks->semantic_context || !callbacks->global_registry ||
      !callbacks->local_registry || !callbacks->runtime_context ||
      !callbacks->options ||
      !callbacks->name_classifier.is_typedef_name || !tokenizer(callbacks)) {
    return 0;
  }
  return 1;
}

node_t *psx_parse_local_declaration_syntax(
    const psx_local_declaration_callbacks_t *callbacks) {
  if (!callbacks_are_complete(callbacks)) return NULL;
  tokenizer_context_t *tk_ctx = tokenizer(callbacks);
  if (curtok(callbacks)->kind == TK_STATIC_ASSERT) {
    psx_parsed_static_assert_declaration_t assertion;
    psx_parse_static_assert_syntax_in_contexts(
          &assertion, callbacks->semantic_context,
          callbacks->global_registry,
          callbacks->local_registry, callbacks->runtime_context,
          &callbacks->name_classifier,
          callbacks);
    return psx_node_new_static_assert_syntax_in(
        ps_parser_runtime_arena(callbacks->runtime_context),
        assertion.condition, assertion.diagnostic_token);
  }

  int is_typedef = curtok(callbacks)->kind == TK_TYPEDEF;
  if (is_typedef)
    tk_set_current_token_ctx(tk_ctx, curtok(callbacks)->next);
  psx_parsed_decl_specifier_t specifier;
  if (!psx_try_parse_decl_specifier_syntax_ex(
          &specifier,
          &(psx_decl_specifier_syntax_options_t){
              .name_classifier = &callbacks->name_classifier,
              .semantic_context = callbacks->semantic_context,
              .global_registry = callbacks->global_registry,
              .local_registry = callbacks->local_registry,
              .runtime_context = callbacks->runtime_context,
          })) {
    diag_report_tokf_in(diagnostics(callbacks),
        DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN, curtok(callbacks), "%s",
        diag_message_for_in(diagnostics(callbacks), DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN));
    ps_parser_mark_recoverable_syntax_error_in(
        callbacks->runtime_context);
    return NULL;
  }
  ps_prepare_decl_specifier_alignments_in_context(
      &specifier, callbacks->semantic_context,
      &callbacks->name_classifier);
  int standalone_tag =
      specifier.source == PSX_PARSED_DECL_TYPE_TAG &&
      curtok(callbacks)->kind == TK_SEMI;
  void *declaration_context = callbacks->begin_declaration(
      callbacks->context, &specifier, is_typedef, standalone_tag);

  if (standalone_tag) {
    tk_expect_ctx(tk_ctx, ';');
    node_t *result =
        callbacks->finish_declaration(declaration_context);
    ps_dispose_decl_specifier_syntax(&specifier);
    return result;
  }

  int declarator_count = 0;
  for (;;) {
    if (++declarator_count > PS_MAX_DECLARATOR_COUNT) {
      ps_diag_ctx_in(diagnostics(callbacks), curtok(callbacks), "decl",
                  diag_message_for_in(diagnostics(callbacks),
                      DIAG_ERR_PARSER_DECLARATOR_LIST_TOO_LONG),
                  PS_MAX_DECLARATOR_COUNT);
    }
    psx_parsed_declarator_t declarator;
    psx_parse_declarator_syntax_tree_into_with_typedef_lookup_in_contexts(
        &declarator, callbacks->semantic_context,
        callbacks->global_registry,
        callbacks->local_registry, callbacks->runtime_context,
        &callbacks->name_classifier);
    ps_parse_runtime_declarator_expressions_in_contexts(
        &declarator, callbacks->semantic_context,
        callbacks->global_registry,
        callbacks->local_registry, callbacks->runtime_context,
        callbacks);
    if (!declarator.identifier) {
      ps_diag_ctx_in(diagnostics(callbacks), curtok(callbacks), "decl", "%s",
                  diag_message_for_in(diagnostics(callbacks),
                      DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
    }

    psx_parsed_initializer_t initializer;
    psx_prepare_optional_initializer_syntax(
        &initializer, callbacks->runtime_context);
    if (initializer.has_initializer && initializer.value_tok &&
        (initializer.value_tok->kind == TK_SEMI ||
         initializer.value_tok->kind == TK_COMMA ||
         initializer.value_tok->kind == TK_RBRACE ||
         initializer.value_tok->kind == TK_EOF)) {
      diag_report_tokf_in(diagnostics(callbacks),
          DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED, initializer.value_tok,
          "%s", diag_message_for_in(diagnostics(callbacks), DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED));
      psx_dispose_declarator_syntax(&declarator);
      if (callbacks->abort_declaration)
        callbacks->abort_declaration(declaration_context);
      else
        callbacks->finish_declaration(declaration_context);
      ps_dispose_decl_specifier_syntax(&specifier);
      ps_parser_mark_recoverable_syntax_error_in(
          callbacks->runtime_context);
      return NULL;
    }
    callbacks->begin_declarator(
        declaration_context, &declarator, &initializer);
    if (initializer.has_initializer) {
      token_t *assign_tok = initializer.assign_tok;
      tk_expect_ctx(tk_ctx, '=');
      psx_parse_initializer_syntax_value_in_contexts(
          &initializer, assign_tok, callbacks->semantic_context,
          callbacks->global_registry,
          callbacks->local_registry, callbacks->runtime_context,
          &callbacks->name_classifier,
          callbacks);
    }
    callbacks->finish_declarator(
        declaration_context, &initializer);
    psx_dispose_declarator_syntax(&declarator);
    if (!tk_consume_ctx(tk_ctx, ',')) break;
  }
  tk_expect_ctx(tk_ctx, ';');
  node_t *result = callbacks->finish_declaration(declaration_context);
  ps_dispose_decl_specifier_syntax(&specifier);
  return result;
}
