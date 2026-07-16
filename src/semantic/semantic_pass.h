#ifndef SEMANTIC_SEMANTIC_PASS_H
#define SEMANTIC_SEMANTIC_PASS_H

#include "../parser/ast.h"
#include "../tokenizer/token.h"
#include "resolved_function.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;

void psx_semantic_resolve_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_t *node, node_function_definition_t *current_func,
    const token_t *fallback_diag_tok);
void psx_semantic_resolve_initializer_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_t *syntax, node_function_definition_t *current_func,
    const token_t *fallback_diag_tok);

#endif
