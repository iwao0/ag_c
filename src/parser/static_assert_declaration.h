#ifndef PARSER_STATIC_ASSERT_DECLARATION_H
#define PARSER_STATIC_ASSERT_DECLARATION_H

#include "ast.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_local_declaration_callbacks_t
    psx_local_declaration_callbacks_t;

typedef struct {
  node_t *condition;
  token_t *diagnostic_token;
} psx_parsed_static_assert_declaration_t;

void psx_parse_static_assert_syntax(
    psx_parsed_static_assert_declaration_t *declaration);
void psx_parse_static_assert_syntax_in_context(
    psx_parsed_static_assert_declaration_t *declaration,
    psx_semantic_context_t *semantic_context,
    const psx_local_declaration_callbacks_t *local_declarations);
void psx_parse_static_assert_syntax_in_contexts(
    psx_parsed_static_assert_declaration_t *declaration,
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    const psx_local_declaration_callbacks_t *local_declarations);

#endif
