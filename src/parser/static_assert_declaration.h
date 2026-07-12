#ifndef PARSER_STATIC_ASSERT_DECLARATION_H
#define PARSER_STATIC_ASSERT_DECLARATION_H

#include "ast.h"

typedef struct {
  node_t *condition;
  token_t *diagnostic_token;
} psx_parsed_static_assert_declaration_t;

void psx_parse_static_assert_syntax(
    psx_parsed_static_assert_declaration_t *declaration);

#endif
