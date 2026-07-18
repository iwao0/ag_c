#include "local_declaration_syntax.h"

#include "aggregate_member_syntax.h"
#include "arena.h"
#include "declaration_binding_events.h"
#include "diag.h"
#include "function_parameter_syntax.h"
#include "node_utils.h"
#include "parser_recovery.h"
#include "runtime_context.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../tokenizer/tokenizer.h"

#include <string.h>

static arena_context_t *arena(
    const psx_local_declaration_callbacks_t *callbacks) {
  return callbacks && callbacks->runtime_context
             ? ps_parser_runtime_arena(callbacks->runtime_context)
             : NULL;
}

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
      !callbacks->runtime_context ||
      !callbacks->name_classifier.is_typedef_name ||
      !callbacks->parse_static_assert ||
      !callbacks->parse_decl_specifier ||
      !callbacks->parse_declarator ||
      !callbacks->parse_initializer ||
      !tokenizer(callbacks)) {
    return 0;
  }
  return 1;
}

static int append_declarator_slot(
    psx_parsed_local_declaration_t *declaration,
    const psx_local_declaration_callbacks_t *callbacks) {
  if (!declaration || !arena(callbacks)) return 0;
  if (declaration->declarator_count >= PS_MAX_DECLARATOR_COUNT) {
    ps_diag_ctx_in(
        diagnostics(callbacks), curtok(callbacks), "decl",
        diag_message_for_in(
            diagnostics(callbacks),
            DIAG_ERR_PARSER_DECLARATOR_LIST_TOO_LONG),
        PS_MAX_DECLARATOR_COUNT);
    return 0;
  }
  int next_count = declaration->declarator_count + 1;
  psx_parsed_declarator_t *declarators = arena_alloc_in(
      arena(callbacks),
      (size_t)next_count * sizeof(*declarators));
  psx_parsed_initializer_t *initializers = arena_alloc_in(
      arena(callbacks),
      (size_t)next_count * sizeof(*initializers));
  if (!declarators || !initializers) return 0;
  if (declaration->declarator_count > 0) {
    memcpy(
        declarators, declaration->declarators,
        (size_t)declaration->declarator_count * sizeof(*declarators));
    memcpy(
        initializers, declaration->initializers,
        (size_t)declaration->declarator_count * sizeof(*initializers));
  }
  declarators[next_count - 1] = (psx_parsed_declarator_t){0};
  initializers[next_count - 1] = (psx_parsed_initializer_t){0};
  declaration->declarators = declarators;
  declaration->initializers = initializers;
  declaration->declarator_count = next_count;
  return 1;
}

node_t *psx_parse_local_declaration_syntax(
    const psx_local_declaration_callbacks_t *callbacks) {
  if (!callbacks_are_complete(callbacks)) return NULL;
  tokenizer_context_t *tk_ctx = tokenizer(callbacks);
  if (curtok(callbacks)->kind == TK_STATIC_ASSERT)
    return callbacks->parse_static_assert(callbacks->context);

  node_local_declaration_t *node = arena_alloc_in(
      arena(callbacks), sizeof(*node));
  psx_parsed_local_declaration_t *declaration = arena_alloc_in(
      arena(callbacks), sizeof(*declaration));
  if (!node || !declaration) return NULL;
  node->base.kind = ND_LOCAL_DECLARATION;
  node->declaration = declaration;
  declaration->diagnostic_token = curtok(callbacks);
  declaration->is_typedef =
      curtok(callbacks)->kind == TK_TYPEDEF;
  if (declaration->is_typedef)
    tk_set_current_token_ctx(tk_ctx, curtok(callbacks)->next);
  if (!callbacks->parse_decl_specifier(
          callbacks->context, &declaration->specifier)) {
    diag_report_tokf_in(diagnostics(callbacks),
        DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN, curtok(callbacks), "%s",
        diag_message_for_in(diagnostics(callbacks), DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN));
    ps_parser_mark_recoverable_syntax_error_in(
        callbacks->runtime_context);
    return NULL;
  }
  declaration->is_extern =
      declaration->specifier.type_spec.is_extern ? 1 : 0;
  declaration->is_static =
      declaration->specifier.type_spec.is_static ? 1 : 0;
  psx_record_decl_specifier_binding_events(
      &declaration->specifier, &callbacks->name_classifier);
  declaration->is_standalone_tag =
      declaration->specifier.source == PSX_PARSED_DECL_TYPE_TAG &&
      curtok(callbacks)->kind == TK_SEMI;

  if (declaration->is_standalone_tag) {
    tk_expect_ctx(tk_ctx, ';');
    return &node->base;
  }

  for (;;) {
    if (!append_declarator_slot(declaration, callbacks)) return NULL;
    psx_parsed_declarator_t *declarator =
        &declaration->declarators[declaration->declarator_count - 1];
    callbacks->parse_declarator(
        callbacks->context, declarator);
    psx_record_declarator_binding_events(
        declarator, &callbacks->name_classifier);
    if (!declarator->identifier) {
      ps_diag_ctx_in(diagnostics(callbacks), curtok(callbacks), "decl", "%s",
                  diag_message_for_in(diagnostics(callbacks),
                      DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
    }

    psx_parsed_initializer_t *initializer =
        &declaration->initializers[declaration->declarator_count - 1];
    psx_prepare_optional_initializer_syntax(
        initializer, callbacks->runtime_context);
    if (initializer->has_initializer && initializer->value_tok &&
        (initializer->value_tok->kind == TK_SEMI ||
         initializer->value_tok->kind == TK_COMMA ||
         initializer->value_tok->kind == TK_RBRACE ||
         initializer->value_tok->kind == TK_EOF)) {
      diag_report_tokf_in(diagnostics(callbacks),
          DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED, initializer->value_tok,
          "%s", diag_message_for_in(diagnostics(callbacks), DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED));
      ps_parser_mark_recoverable_syntax_error_in(
          callbacks->runtime_context);
      return NULL;
    }
    ps_name_classifier_declare(
        &callbacks->name_classifier,
        (token_t *)declarator->identifier, declaration->is_typedef);
    if (initializer->has_initializer) {
      token_t *assign_tok = initializer->assign_tok;
      tk_expect_ctx(tk_ctx, '=');
      callbacks->parse_initializer(
          callbacks->context, initializer, assign_tok);
    }
    if (!tk_consume_ctx(tk_ctx, ',')) break;
  }
  tk_expect_ctx(tk_ctx, ';');
  return &node->base;
}
