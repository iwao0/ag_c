#ifndef LOWERING_SEMANTIC_LOWERING_PASS_H
#define LOWERING_SEMANTIC_LOWERING_PASS_H

#include "../parser/ast.h"

node_t *psx_lower_semantic_tree(
    node_t *node, const token_t *fallback_diag_tok);
node_t *psx_lower_semantic_initializer_syntax(
    node_t *syntax, const token_t *fallback_diag_tok);
void psx_lower_implicit_conversions(
    node_t *node, node_function_definition_t *current_func,
    const token_t *fallback_diag_tok);

#endif
