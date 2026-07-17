#ifndef SEMANTIC_TYPED_HIR_TREE_H
#define SEMANTIC_TYPED_HIR_TREE_H

#include "../hir/hir.h"

typedef struct psx_typed_hir_tree_t psx_typed_hir_tree_t;

psx_hir_node_kind_t psx_typed_hir_tree_root_kind(
    const psx_typed_hir_tree_t *tree);
int psx_typed_hir_tree_root_storage_offset(
    const psx_typed_hir_tree_t *tree);

#endif
