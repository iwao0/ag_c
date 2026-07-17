#include "typed_hir_tree.h"

#include "semantic_node_internal.h"
#include "typed_hir_tree_internal.h"

psx_hir_node_kind_t psx_typed_hir_tree_root_kind(
    const psx_typed_hir_tree_t *tree) {
  return tree && tree->root ? tree->root->spec.kind : PSX_HIR_NOP;
}

int psx_typed_hir_tree_root_storage_offset(
    const psx_typed_hir_tree_t *tree) {
  return tree && tree->root ? tree->root->spec.storage_offset : 0;
}
