#ifndef PARSER_TOPLEVEL_DECLARATION_SYNTAX_H
#define PARSER_TOPLEVEL_DECLARATION_SYNTAX_H

#include "declaration_syntax.h"
#include "initializer_syntax.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef struct {
  psx_parsed_decl_specifier_t specifier;
  psx_parsed_declarator_t *declarators;
  psx_parsed_initializer_t *initializers;
  int declarator_count;
  int is_typedef;
  int is_extern;
  int is_static;
  int is_thread_local;
  int is_standalone_tag;
  int applied_during_parse;
  token_t *diagnostic_token;
} psx_parsed_toplevel_declaration_t;

typedef struct {
  void *context;
  void *(*begin_declaration)(
      void *context, psx_parsed_toplevel_declaration_t *declaration);
  void (*begin_declarator)(
      void *declaration_context,
      psx_parsed_declarator_t *declarator,
      psx_parsed_initializer_t *initializer);
  void (*finish_declarator)(
      void *declaration_context,
      psx_parsed_initializer_t *initializer);
  void (*finish_declaration)(void *declaration_context);
  void (*abort_declaration)(void *declaration_context);
} psx_toplevel_declaration_callbacks_t;

int psx_parse_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_callbacks_t *callbacks);
int psx_parse_toplevel_declaration_head_syntax(
    psx_parsed_toplevel_declaration_t *declaration);
int psx_parse_toplevel_declaration_head_syntax_in_context(
    psx_parsed_toplevel_declaration_t *declaration,
    psx_semantic_context_t *semantic_context);
int psx_finish_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_callbacks_t *callbacks);
int psx_finish_toplevel_declaration_syntax_in_context(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_callbacks_t *callbacks,
    psx_semantic_context_t *semantic_context);
void ps_dispose_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration);

#endif
