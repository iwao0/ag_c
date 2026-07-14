#ifndef SEMANTIC_SEMANTIC_PASS_H
#define SEMANTIC_SEMANTIC_PASS_H

#include "../parser/ast.h"
#include "../tokenizer/token.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

void psx_semantic_resolve_tree(
    node_t *node, node_function_definition_t *current_func,
    const token_t *fallback_diag_tok);
void psx_semantic_resolve_tree_in_context(
    psx_semantic_context_t *semantic_context,
    node_t *node, node_function_definition_t *current_func,
    const token_t *fallback_diag_tok);
void psx_semantic_resolve_initializer_tree(
    node_t *syntax, node_function_definition_t *current_func,
    const token_t *fallback_diag_tok);
void psx_semantic_resolve_initializer_tree_in_context(
    psx_semantic_context_t *semantic_context,
    node_t *syntax, node_function_definition_t *current_func,
    const token_t *fallback_diag_tok);

#endif
