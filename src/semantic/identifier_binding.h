#ifndef SEMANTIC_IDENTIFIER_BINDING_H
#define SEMANTIC_IDENTIFIER_BINDING_H

#include "../parser/ast.h"

node_t *psx_bind_identifier_tree(
    node_t *node, const token_t *fallback_diag_tok);
node_t *psx_bind_identifier_initializer_tree(
    node_t *syntax, const token_t *fallback_diag_tok);

#endif
