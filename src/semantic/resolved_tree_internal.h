#ifndef SEMANTIC_RESOLVED_TREE_INTERNAL_H
#define SEMANTIC_RESOLVED_TREE_INTERNAL_H

#include "resolved_tree.h"

typedef struct psx_resolved_hir_node_t psx_resolved_hir_node_t;

int psx_resolved_tree_publish_hir_root(
    psx_resolved_tree_t *tree, psx_resolved_hir_node_t *root);
const psx_resolved_hir_node_t *psx_resolved_tree_hir_root(
    const psx_resolved_tree_t *tree);

#endif
