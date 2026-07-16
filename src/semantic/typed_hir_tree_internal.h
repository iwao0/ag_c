#ifndef SEMANTIC_TYPED_HIR_TREE_INTERNAL_H
#define SEMANTIC_TYPED_HIR_TREE_INTERNAL_H

#include "typed_hir_tree.h"

typedef struct psx_resolved_hir_node_t psx_resolved_hir_node_t;

struct psx_typed_hir_tree_t {
  psx_resolved_hir_node_t *root;
};

#endif
