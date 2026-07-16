#ifndef PARSER_STATIC_ASSERT_DECLARATION_H
#define PARSER_STATIC_ASSERT_DECLARATION_H

#include "ast.h"
#include "name_classifier.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
typedef struct psx_local_declaration_callbacks_t
    psx_local_declaration_callbacks_t;

typedef struct {
  node_t *condition;
  token_t *diagnostic_token;
} psx_parsed_static_assert_declaration_t;

void psx_parse_static_assert_syntax_in_contexts(
    psx_parsed_static_assert_declaration_t *declaration,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations);

#endif
