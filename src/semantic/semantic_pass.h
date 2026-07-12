#ifndef SEMANTIC_SEMANTIC_PASS_H
#define SEMANTIC_SEMANTIC_PASS_H

#include "../parser/ast.h"
#include "../tokenizer/token.h"

void psx_semantic_resolve_tree(
    node_t *node, node_func_t *current_func,
    const token_t *fallback_diag_tok);
void psx_semantic_resolve_initializer_tree(
    node_t *syntax, node_func_t *current_func,
    const token_t *fallback_diag_tok);

#endif
