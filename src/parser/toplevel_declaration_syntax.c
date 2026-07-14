#include "toplevel_declaration_syntax.h"

#include "core.h"
#include "diag.h"
#include "local_registry.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../tokenizer/tokenizer.h"

#include <stdlib.h>
#include <string.h>

static token_t *current_token(void) { return tk_get_current_token(); }

static int is_toplevel_typedef_name(token_t *token, void *context) {
  return psx_ctx_is_typedef_name_token_in(context, token);
}

static void require_declarator_name(
    const psx_parsed_declarator_t *declarator) {
  if (declarator && declarator->identifier) return;
  diag_emit_tokf(
      DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED, current_token(), "%s",
      diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
}

static void append_declarator_slot(
    psx_parsed_toplevel_declaration_t *declaration) {
  if (declaration->declarator_count >= PS_MAX_DECLARATOR_COUNT) {
    ps_diag_ctx(
        current_token(), "decl",
        diag_message_for(DIAG_ERR_PARSER_DECLARATOR_LIST_TOO_LONG),
        PS_MAX_DECLARATOR_COUNT);
  }
  int next_count = declaration->declarator_count + 1;
  psx_parsed_declarator_t *grown = realloc(
      declaration->declarators,
      sizeof(*declaration->declarators) * (size_t)next_count);
  if (!grown) {
    ps_diag_ctx(current_token(), "decl",
                "top-level declaration syntax allocation failed");
  }
  declaration->declarators = grown;
  psx_parsed_initializer_t *grown_initializers = realloc(
      declaration->initializers,
      sizeof(*declaration->initializers) * (size_t)next_count);
  if (!grown_initializers) {
    ps_diag_ctx(current_token(), "decl",
                "top-level initializer syntax allocation failed");
  }
  declaration->initializers = grown_initializers;
  declaration->initializers[declaration->declarator_count] =
      (psx_parsed_initializer_t){0};
  declaration->declarator_count = next_count;
}

static int parse_declarator_head(
    psx_parsed_toplevel_declaration_t *declaration,
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry) {
  append_declarator_slot(declaration);
  psx_parsed_declarator_t *declarator =
      &declaration->declarators[declaration->declarator_count - 1];
  if (!psx_try_parse_toplevel_declarator_syntax_tree_with_typedef_lookup_in_contexts(
          declarator, semantic_context, local_registry,
          is_toplevel_typedef_name, semantic_context)) return 0;
  ps_prepare_constant_declarator_expressions_in_context(
      declarator, semantic_context);
  require_declarator_name(declarator);
  return 1;
}

int psx_parse_toplevel_declaration_head_syntax(
    psx_parsed_toplevel_declaration_t *declaration) {
  return psx_parse_toplevel_declaration_head_syntax_in_contexts(
      declaration, ps_ctx_active(), ps_local_registry_active());
}

int psx_parse_toplevel_declaration_head_syntax_in_contexts(
    psx_parsed_toplevel_declaration_t *declaration,
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry) {
  if (!declaration || !semantic_context || !local_registry) return 0;
  *declaration = (psx_parsed_toplevel_declaration_t){0};
  declaration->diagnostic_token = current_token();
  if (current_token()->kind == TK_TYPEDEF) {
    declaration->is_typedef = 1;
    tk_set_current_token(current_token()->next);
  }

  if (!psx_try_parse_decl_specifier_syntax_ex(
          &declaration->specifier,
          &(psx_decl_specifier_syntax_options_t){
              .is_typedef_name = is_toplevel_typedef_name,
              .context = semantic_context,
              .semantic_context = semantic_context,
              .local_registry = local_registry,
              .allow_implicit_int = 0,
          })) {
    diag_report_tokf(
        DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN,
        current_token(), "%s",
        diag_message_for(DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN));
    return 0;
  }
  ps_prepare_decl_specifier_alignments_in_context(
      &declaration->specifier, semantic_context);
  declaration->is_extern = declaration->specifier.type_spec.is_extern;
  declaration->is_static = declaration->specifier.type_spec.is_static;
  declaration->is_thread_local =
      declaration->specifier.type_spec.is_thread_local;
  declaration->is_standalone_tag =
      declaration->specifier.source == PSX_PARSED_DECL_TYPE_TAG &&
      current_token()->kind == TK_SEMI;
  if (!declaration->is_standalone_tag &&
      !parse_declarator_head(declaration, semantic_context, local_registry))
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

static void abort_toplevel_declaration(
    const psx_toplevel_declaration_callbacks_t *callbacks,
    void *declaration_context) {
  if (callbacks && callbacks->abort_declaration)
    callbacks->abort_declaration(declaration_context);
  else if (callbacks && callbacks->finish_declaration)
    callbacks->finish_declaration(declaration_context);
}

int psx_finish_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_callbacks_t *callbacks) {
  psx_semantic_context_t *semantic_context =
      callbacks && callbacks->semantic_context && callbacks->local_registry
          ? callbacks->semantic_context : ps_ctx_active();
  psx_local_registry_t *local_registry =
      callbacks && callbacks->semantic_context && callbacks->local_registry
          ? callbacks->local_registry : ps_local_registry_active();
  return psx_finish_toplevel_declaration_syntax_in_contexts(
      declaration, callbacks, semantic_context, local_registry);
}

int psx_finish_toplevel_declaration_syntax_in_contexts(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_callbacks_t *callbacks,
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry) {
  if (!declaration || !semantic_context || !local_registry) return 0;
  void *declaration_context = callbacks && callbacks->begin_declaration
      ? callbacks->begin_declaration(callbacks->context, declaration)
      : NULL;
  declaration->applied_during_parse = callbacks ? 1 : 0;
  if (declaration->is_standalone_tag) {
    tk_expect(';');
    if (callbacks && callbacks->finish_declaration)
      callbacks->finish_declaration(declaration_context);
    return 1;
  }

  for (;;) {
    psx_parsed_declarator_t *declarator =
        &declaration->declarators[declaration->declarator_count - 1];
    psx_parsed_initializer_t *initializer =
        &declaration->initializers[declaration->declarator_count - 1];
    psx_prepare_optional_initializer_syntax(initializer);
    if (initializer_value_is_missing(initializer)) {
      diag_report_tokf(
          DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED, initializer->value_tok,
          "%s", diag_message_for(DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED));
      abort_toplevel_declaration(callbacks, declaration_context);
      return 0;
    }
    if (callbacks && callbacks->begin_declarator)
      callbacks->begin_declarator(
          declaration_context, declarator, initializer);
    if (initializer->has_initializer) {
      token_t *assign_tok = initializer->assign_tok;
      tk_expect('=');
      psx_parse_initializer_syntax_value_in_contexts(
          initializer, assign_tok, semantic_context,
          local_registry, NULL);
    }
    if (callbacks && callbacks->finish_declarator)
      callbacks->finish_declarator(declaration_context, initializer);
    if (!tk_consume(',')) break;
    if (!parse_declarator_head(
            declaration, semantic_context, local_registry)) {
      abort_toplevel_declaration(callbacks, declaration_context);
      return 0;
    }
  }
  tk_expect(';');
  if (callbacks && callbacks->finish_declaration)
    callbacks->finish_declaration(declaration_context);
  return 1;
}

int psx_parse_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_callbacks_t *callbacks) {
  if (!psx_parse_toplevel_declaration_head_syntax(declaration)) return 0;
  return psx_finish_toplevel_declaration_syntax(declaration, callbacks);
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
