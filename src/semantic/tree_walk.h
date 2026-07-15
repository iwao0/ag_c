#ifndef SEMANTIC_TREE_WALK_H
#define SEMANTIC_TREE_WALK_H

#include "../parser/ast.h"

typedef int (*psx_semantic_tree_visitor_t)(const node_t *node, void *user);
typedef int (*psx_semantic_tree_mutating_visitor_t)(node_t *node, void *user);

int psx_walk_semantic_tree(
    const node_t *root, psx_semantic_tree_visitor_t visitor, void *user);
int psx_walk_semantic_tree_mut(
    node_t *root, psx_semantic_tree_mutating_visitor_t visitor, void *user);

#endif
