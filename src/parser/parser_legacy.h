#ifndef PARSER_PARSER_LEGACY_H
#define PARSER_PARSER_LEGACY_H

#include "local_declaration_syntax.h"

typedef struct node_t node_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
typedef struct tokenizer_context_t tokenizer_context_t;
typedef struct token_t token_t;

node_t *ps_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_local_declaration_callbacks_t *local_declarations,
    tokenizer_context_t *tk_ctx, token_t *start);

#endif
