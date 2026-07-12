#ifndef PARSER_TOPLEVEL_DECLARATION_SYNTAX_H
#define PARSER_TOPLEVEL_DECLARATION_SYNTAX_H

#include "declaration_syntax.h"
#include "initializer_syntax.h"

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
} psx_toplevel_declaration_callbacks_t;

void ps_parse_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_callbacks_t *callbacks);
void ps_parse_toplevel_declaration_head_syntax(
    psx_parsed_toplevel_declaration_t *declaration);
void ps_finish_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_callbacks_t *callbacks);
void ps_dispose_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration);

#endif
