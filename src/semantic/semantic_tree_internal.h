#ifndef SEMANTIC_SEMANTIC_TREE_INTERNAL_H
#define SEMANTIC_SEMANTIC_TREE_INTERNAL_H

#include "semantic_tree.h"

#include "../parser/node_fwd.h"

typedef struct arena_context_t arena_context_t;
typedef struct psx_semantic_node_t psx_semantic_node_t;
typedef struct psx_typed_hir_tree_t psx_typed_hir_tree_t;

psx_semantic_tree_t *psx_semantic_tree_create_from_syntax(
    arena_context_t *arena_context, const node_t *syntax_root);
psx_semantic_tree_t *psx_semantic_tree_create_with_compatibility_root(
    arena_context_t *arena_context, node_t *root);
node_t *psx_semantic_tree_compatibility_root_mut(
    psx_semantic_tree_t *tree);
const node_t *psx_semantic_tree_compatibility_root(
    const psx_semantic_tree_t *tree);
int psx_semantic_tree_replace_compatibility_root(
    psx_semantic_tree_t *tree, node_t *root);
const psx_semantic_node_t *psx_semantic_tree_root(
    const psx_semantic_tree_t *tree);
int psx_semantic_tree_set_root(
    psx_semantic_tree_t *tree, psx_semantic_node_t *root);
psx_typed_hir_tree_t *psx_semantic_tree_typed_hir_view(
    const psx_semantic_tree_t *tree);

#endif
