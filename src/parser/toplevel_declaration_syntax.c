#include "toplevel_declaration_syntax.h"

#include "core.h"
#include "diag.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../tokenizer/tokenizer.h"

#include <stdlib.h>
#include <string.h>

static token_t *current_token(void) { return tk_get_current_token(); }

static int is_toplevel_typedef_name(token_t *token, void *context) {
  (void)context;
  return psx_ctx_is_typedef_name_token(token);
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

static void parse_declarator_head(
    psx_parsed_toplevel_declaration_t *declaration) {
  append_declarator_slot(declaration);
  psx_parsed_declarator_t *declarator =
      &declaration->declarators[declaration->declarator_count - 1];
  *declarator = ps_parse_declarator_syntax_tree();
  ps_prepare_constant_declarator_expressions(declarator);
  require_declarator_name(declarator);
}

void ps_parse_toplevel_declaration_head_syntax(
    psx_parsed_toplevel_declaration_t *declaration) {
  if (!declaration) return;
  *declaration = (psx_parsed_toplevel_declaration_t){0};
  declaration->diagnostic_token = current_token();
  if (current_token()->kind == TK_TYPEDEF) {
    declaration->is_typedef = 1;
    tk_set_current_token(current_token()->next);
  }

  ps_parse_decl_specifier_syntax_ex(
      &declaration->specifier,
      &(psx_decl_specifier_syntax_options_t){
          .is_typedef_name = is_toplevel_typedef_name,
          .allow_implicit_int = 1,
      });
  ps_prepare_decl_specifier_alignments(&declaration->specifier);
  declaration->is_extern = declaration->specifier.type_spec.is_extern;
  declaration->is_static = declaration->specifier.type_spec.is_static;
  declaration->is_thread_local =
      declaration->specifier.type_spec.is_thread_local;
  declaration->is_standalone_tag =
      declaration->specifier.source == PSX_PARSED_DECL_TYPE_TAG &&
      current_token()->kind == TK_SEMI;
  if (!declaration->is_standalone_tag) parse_declarator_head(declaration);
}

void ps_finish_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_callbacks_t *callbacks) {
  if (!declaration) return;
  void *declaration_context = callbacks && callbacks->begin_declaration
      ? callbacks->begin_declaration(callbacks->context, declaration)
      : NULL;
  declaration->applied_during_parse = callbacks ? 1 : 0;
  if (declaration->is_standalone_tag) {
    tk_expect(';');
    if (callbacks && callbacks->finish_declaration)
      callbacks->finish_declaration(declaration_context);
    return;
  }

  for (;;) {
    psx_parsed_declarator_t *declarator =
        &declaration->declarators[declaration->declarator_count - 1];
    psx_parsed_initializer_t *initializer =
        &declaration->initializers[declaration->declarator_count - 1];
    ps_prepare_optional_initializer_syntax(initializer);
    if (callbacks && callbacks->begin_declarator)
      callbacks->begin_declarator(
          declaration_context, declarator, initializer);
    if (initializer->has_initializer) {
      token_t *assign_tok = initializer->assign_tok;
      tk_expect('=');
      ps_parse_initializer_syntax_value(initializer, assign_tok);
    }
    if (callbacks && callbacks->finish_declarator)
      callbacks->finish_declarator(declaration_context, initializer);
    if (!tk_consume(',')) break;
    parse_declarator_head(declaration);
  }
  tk_expect(';');
  if (callbacks && callbacks->finish_declaration)
    callbacks->finish_declaration(declaration_context);
}

void ps_parse_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_callbacks_t *callbacks) {
  ps_parse_toplevel_declaration_head_syntax(declaration);
  ps_finish_toplevel_declaration_syntax(declaration, callbacks);
}

void ps_dispose_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration) {
  if (!declaration) return;
  for (int i = 0; i < declaration->declarator_count; i++)
    ps_dispose_declarator_syntax(&declaration->declarators[i]);
  free(declaration->declarators);
  free(declaration->initializers);
  ps_dispose_decl_specifier_syntax(&declaration->specifier);
  memset(declaration, 0, sizeof(*declaration));
}
