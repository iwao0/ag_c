#ifndef PARSER_STATIC_ASSERT_DECLARATION_H
#define PARSER_STATIC_ASSERT_DECLARATION_H

#include "ast.h"

typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

typedef struct {
  void *context;
  psx_parser_runtime_context_t *runtime_context;
  node_t *(*parse_assignment_expression)(void *context);
} psx_static_assert_syntax_context_t;

typedef struct {
  node_t *condition;
  token_t *diagnostic_token;
} psx_parsed_static_assert_declaration_t;

void psx_parse_static_assert_syntax_with_context(
    psx_parsed_static_assert_declaration_t *declaration,
    const psx_static_assert_syntax_context_t *context);

#endif
