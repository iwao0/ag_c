#ifndef SEMANTIC_TYPED_HIR_TREE_INTERNAL_H
#define SEMANTIC_TYPED_HIR_TREE_INTERNAL_H

#include "typed_hir_tree.h"

typedef struct psx_semantic_node_t psx_semantic_node_t;

struct psx_typed_hir_tree_t {
  const psx_semantic_node_t *root;
};

#endif
