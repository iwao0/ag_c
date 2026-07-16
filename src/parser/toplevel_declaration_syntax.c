#include "toplevel_declaration_syntax.h"

#include "core.h"
#include "diag.h"
#include "runtime_context.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../tokenizer/tokenizer.h"

#include <stdlib.h>
#include <string.h>

static tokenizer_context_t *tokenizer(
    psx_parser_runtime_context_t *runtime_context) {
  return ps_parser_runtime_tokenizer(runtime_context);
}

static token_t *current_token(
    psx_parser_runtime_context_t *runtime_context) {
  return tk_get_current_token_ctx(tokenizer(runtime_context));
}

static ag_diagnostic_context_t *diagnostics(
    psx_parser_runtime_context_t *runtime_context) {
  return ps_parser_runtime_diagnostics(runtime_context);
}

static void require_declarator_name(
    const psx_parsed_declarator_t *declarator,
    psx_parser_runtime_context_t *runtime_context) {
  if (declarator && declarator->identifier) return;
  diag_emit_tokf_in(diagnostics(runtime_context),
      DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED,
      current_token(runtime_context), "%s",
      diag_message_for_in(diagnostics(runtime_context), DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
}

static void append_declarator_slot(
    psx_parsed_toplevel_declaration_t *declaration,
    psx_parser_runtime_context_t *runtime_context) {
  if (declaration->declarator_count >= PS_MAX_DECLARATOR_COUNT) {
    ps_diag_ctx_in(diagnostics(runtime_context),
        current_token(runtime_context), "decl",
        diag_message_for_in(diagnostics(runtime_context), DIAG_ERR_PARSER_DECLARATOR_LIST_TOO_LONG),
        PS_MAX_DECLARATOR_COUNT);
  }
  int next_count = declaration->declarator_count + 1;
  psx_parsed_declarator_t *grown = realloc(
      declaration->declarators,
      sizeof(*declaration->declarators) * (size_t)next_count);
  if (!grown) {
    ps_diag_ctx_in(diagnostics(runtime_context), current_token(runtime_context), "decl",
                "top-level declaration syntax allocation failed");
  }
  declaration->declarators = grown;
  psx_parsed_initializer_t *grown_initializers = realloc(
      declaration->initializers,
      sizeof(*declaration->initializers) * (size_t)next_count);
  if (!grown_initializers) {
    ps_diag_ctx_in(diagnostics(runtime_context), current_token(runtime_context), "decl",
                "top-level initializer syntax allocation failed");
  }
  declaration->initializers = grown_initializers;
  declaration->initializers[declaration->declarator_count] =
      (psx_parsed_initializer_t){0};
  declaration->declarator_count = next_count;
}

static int parse_declarator_head(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_decl_specifier_syntax_options_t *options) {
  psx_parser_runtime_context_t *runtime_context =
      options ? options->runtime_context : NULL;
  append_declarator_slot(declaration, runtime_context);
  psx_parsed_declarator_t *declarator =
      &declaration->declarators[declaration->declarator_count - 1];
  if (!psx_try_parse_toplevel_declarator_syntax_tree_with_typedef_lookup_in_contexts(
          declarator, options)) return 0;
  ps_parse_runtime_declarator_expressions_with_options(
      declarator, options);
  ps_prepare_constant_declarator_expressions_in_context(
      declarator, options->semantic_context,
      options->name_classifier);
  require_declarator_name(declarator, runtime_context);
  return 1;
}

int psx_parse_toplevel_declaration_head_syntax_with_context(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_syntax_context_t *context) {
  psx_semantic_context_t *semantic_context =
      context ? context->semantic_context : NULL;
  psx_parser_runtime_context_t *runtime_context =
      context ? context->runtime_context : NULL;
  const psx_name_classifier_t *name_classifier =
      context ? &context->name_classifier : NULL;
  if (!declaration || !context || !semantic_context ||
      !runtime_context || !context->parse_assignment_expression ||
      !tokenizer(runtime_context))
    return 0;
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  psx_decl_specifier_syntax_options_t syntax_options = {
      .name_classifier = name_classifier,
      .expression_context = context->context,
      .parse_assignment_expression =
          context->parse_assignment_expression,
      .semantic_context = semantic_context,
      .runtime_context = runtime_context,
  };
  *declaration = (psx_parsed_toplevel_declaration_t){0};
  declaration->diagnostic_token = current_token(runtime_context);
  if (current_token(runtime_context)->kind == TK_TYPEDEF) {
    declaration->is_typedef = 1;
    tk_set_current_token_ctx(
        tk_ctx, current_token(runtime_context)->next);
  }

  if (!psx_try_parse_decl_specifier_syntax_ex(
          &declaration->specifier,
          &(psx_decl_specifier_syntax_options_t){
              .name_classifier = syntax_options.name_classifier,
              .expression_context =
                  syntax_options.expression_context,
              .parse_assignment_expression =
                  syntax_options.parse_assignment_expression,
              .semantic_context = syntax_options.semantic_context,
              .runtime_context = syntax_options.runtime_context,
              .allow_implicit_int = 0,
          })) {
    diag_report_tokf_in(diagnostics(runtime_context),
        DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN,
        current_token(runtime_context), "%s",
        diag_message_for_in(diagnostics(runtime_context), DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN));
    return 0;
  }
  ps_prepare_decl_specifier_alignments_in_context(
      &declaration->specifier, semantic_context, name_classifier);
  declaration->is_extern = declaration->specifier.type_spec.is_extern;
  declaration->is_static = declaration->specifier.type_spec.is_static;
  declaration->is_thread_local =
      declaration->specifier.type_spec.is_thread_local;
  declaration->is_standalone_tag =
      declaration->specifier.source == PSX_PARSED_DECL_TYPE_TAG &&
      current_token(runtime_context)->kind == TK_SEMI;
  if (!declaration->is_standalone_tag &&
      !parse_declarator_head(
          declaration, &syntax_options))
    return 0;
  return 1;
}

static int initializer_value_is_missing(
    const psx_parsed_initializer_t *initializer) {
  if (!initializer || !initializer->has_initializer || !initializer->value_tok)
    return 0;
  token_kind_t kind = initializer->value_tok->kind;
  return kind == TK_SEMI || kind == TK_COMMA || kind == TK_RBRACE ||
         kind == TK_EOF;
}

int psx_finish_toplevel_declaration_syntax_with_context(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_syntax_context_t *context) {
  psx_semantic_context_t *semantic_context =
      context ? context->semantic_context : NULL;
  psx_parser_runtime_context_t *runtime_context =
      context ? context->runtime_context : NULL;
  if (!declaration || !context || !semantic_context ||
      !runtime_context || !context->parse_assignment_expression ||
      !tokenizer(runtime_context))
    return 0;
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  psx_initializer_syntax_context_t initializer_syntax = {
      .context = context->context,
      .runtime_context = runtime_context,
      .parse_assignment_expression =
          context->parse_assignment_expression,
      .record_unsupported_gnu_extension =
          context->record_unsupported_gnu_extension,
  };
  if (declaration->is_standalone_tag) {
    tk_expect_ctx(tk_ctx, ';');
    return 1;
  }

  for (;;) {
    psx_parsed_declarator_t *declarator =
        &declaration->declarators[declaration->declarator_count - 1];
    psx_parsed_initializer_t *initializer =
        &declaration->initializers[declaration->declarator_count - 1];
    psx_prepare_optional_initializer_syntax(
        initializer, runtime_context);
    if (initializer_value_is_missing(initializer)) {
      diag_report_tokf_in(diagnostics(runtime_context),
          DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED, initializer->value_tok,
          "%s", diag_message_for_in(diagnostics(runtime_context), DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED));
      return 0;
    }
    ps_name_classifier_declare(
        &context->name_classifier,
        (token_t *)declarator->identifier,
        declaration->is_typedef);
    if (initializer->has_initializer) {
      token_t *assign_tok = initializer->assign_tok;
      tk_expect_ctx(tk_ctx, '=');
      psx_parse_initializer_syntax_value_with_context(
          initializer, assign_tok, &initializer_syntax);
    }
    if (!tk_consume_ctx(tk_ctx, ',')) break;
    if (!parse_declarator_head(
            declaration,
            &(psx_decl_specifier_syntax_options_t){
                .name_classifier = &context->name_classifier,
                .expression_context = context->context,
                .parse_assignment_expression =
                    context->parse_assignment_expression,
                .semantic_context = semantic_context,
                .runtime_context = runtime_context,
            })) {
      return 0;
    }
  }
  tk_expect_ctx(tk_ctx, ';');
  return 1;
}

int psx_parse_toplevel_declaration_syntax_with_context(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_syntax_context_t *context) {
  if (!psx_parse_toplevel_declaration_head_syntax_with_context(
          declaration, context))
    return 0;
  return psx_finish_toplevel_declaration_syntax_with_context(
      declaration, context);
}

void ps_dispose_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration) {
  if (!declaration) return;
  for (int i = 0; i < declaration->declarator_count; i++)
    psx_dispose_declarator_syntax(&declaration->declarators[i]);
  free(declaration->declarators);
  free(declaration->initializers);
  ps_dispose_decl_specifier_syntax(&declaration->specifier);
  memset(declaration, 0, sizeof(*declaration));
}
