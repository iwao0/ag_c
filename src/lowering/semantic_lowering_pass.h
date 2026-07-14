#ifndef LOWERING_SEMANTIC_LOWERING_PASS_H
#define LOWERING_SEMANTIC_LOWERING_PASS_H

#include "../parser/ast.h"

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;

node_t *psx_lower_semantic_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_t *node, const token_t *fallback_diag_tok);
node_t *psx_lower_semantic_initializer_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_t *syntax, const token_t *fallback_diag_tok);

node_t *psx_lower_semantic_tree_in(
    psx_local_registry_t *local_registry,
    node_t *node, const token_t *fallback_diag_tok);
node_t *psx_lower_semantic_initializer_syntax_in(
    psx_local_registry_t *local_registry,
    node_t *syntax, const token_t *fallback_diag_tok);

node_t *psx_lower_semantic_tree(
    node_t *node, const token_t *fallback_diag_tok);
node_t *psx_lower_semantic_initializer_syntax(
    node_t *syntax, const token_t *fallback_diag_tok);
void psx_lower_implicit_conversions(
    node_t *node, node_function_definition_t *current_func,
    const token_t *fallback_diag_tok);

#endif
