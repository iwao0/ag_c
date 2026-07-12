#ifndef PARSER_INITIALIZER_SYNTAX_H
#define PARSER_INITIALIZER_SYNTAX_H

#include "ast.h"

typedef struct {
  int has_initializer;
  psx_decl_init_kind_t kind;
  node_t *value;
  token_t *assign_tok;
  token_t *value_tok;
} psx_parsed_initializer_t;

node_t *psx_parse_initializer_syntax_list(void);
void psx_prepare_optional_initializer_syntax(
    psx_parsed_initializer_t *out);
void psx_parse_initializer_syntax_value(
    psx_parsed_initializer_t *out, token_t *assign_tok);

#endif
