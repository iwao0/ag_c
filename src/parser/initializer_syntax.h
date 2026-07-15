#ifndef PARSER_INITIALIZER_SYNTAX_H
#define PARSER_INITIALIZER_SYNTAX_H

#include "ast.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_local_declaration_callbacks_t
    psx_local_declaration_callbacks_t;

typedef struct {
  int has_initializer;
  psx_decl_init_kind_t kind;
  node_t *value;
  token_t *assign_tok;
  token_t *value_tok;
} psx_parsed_initializer_t;

node_t *psx_parse_initializer_syntax_list_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_local_declaration_callbacks_t *local_declarations);
void psx_prepare_optional_initializer_syntax(
    psx_parsed_initializer_t *out);
void psx_parse_initializer_syntax_value_in_contexts(
    psx_parsed_initializer_t *out, token_t *assign_tok,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_local_declaration_callbacks_t *local_declarations);

#endif
