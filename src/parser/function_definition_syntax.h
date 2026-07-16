#ifndef PARSER_FUNCTION_DEFINITION_SYNTAX_H
#define PARSER_FUNCTION_DEFINITION_SYNTAX_H

#include "declaration_syntax.h"
#include "toplevel_declaration_syntax.h"

typedef struct {
  psx_parsed_decl_specifier_t return_specifier;
  psx_parsed_declarator_t declarator;
  node_t *body;
  int is_static;
  int has_implicit_int_return;
  token_t *diagnostic_token;
} psx_parsed_function_definition_t;

void ps_dispose_function_definition_syntax(
    psx_parsed_function_definition_t *definition);
void psx_move_toplevel_declaration_head_to_function_definition(
    psx_parsed_toplevel_declaration_t *declaration,
    psx_parsed_function_definition_t *definition);
#endif
