#ifndef SEMANTIC_SEMANTIC_TREE_INTERNAL_H
#define SEMANTIC_SEMANTIC_TREE_INTERNAL_H

#include "semantic_tree.h"

typedef struct arena_context_t arena_context_t;
typedef struct psx_semantic_node_t psx_semantic_node_t;
typedef struct psx_typed_hir_tree_t psx_typed_hir_tree_t;

psx_semantic_tree_t *psx_semantic_tree_create(
    arena_context_t *arena_context);
const psx_semantic_node_t *psx_semantic_tree_root(
    const psx_semantic_tree_t *tree);
int psx_semantic_tree_set_root(
    psx_semantic_tree_t *tree, psx_semantic_node_t *root);
psx_typed_hir_tree_t *psx_semantic_tree_typed_hir_view(
    const psx_semantic_tree_t *tree);

#endif
