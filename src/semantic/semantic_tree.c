#include "semantic_tree_internal.h"

#include "../parser/arena.h"
#include "typed_hir_tree_internal.h"

struct psx_semantic_tree_t {
  psx_semantic_node_t *root;
  psx_typed_hir_tree_t *typed_hir_view;
};

psx_semantic_tree_t *psx_semantic_tree_create(
    arena_context_t *arena_context) {
  if (!arena_context) return NULL;
  psx_semantic_tree_t *tree = arena_alloc_in(
      arena_context, sizeof(*tree));
  if (!tree) return NULL;
  tree->typed_hir_view = arena_alloc_in(
      arena_context, sizeof(*tree->typed_hir_view));
  if (!tree->typed_hir_view) return NULL;
  return tree;
}

const psx_semantic_node_t *psx_semantic_tree_root(
    const psx_semantic_tree_t *tree) {
  return tree ? tree->root : NULL;
}

int psx_semantic_tree_set_root(
    psx_semantic_tree_t *tree, psx_semantic_node_t *root) {
  if (!tree || !root || tree->root) return 0;
  tree->root = root;
  tree->typed_hir_view->root = root;
  return 1;
}

psx_typed_hir_tree_t *psx_semantic_tree_typed_hir_view(
    const psx_semantic_tree_t *tree) {
  return tree && tree->root ? tree->typed_hir_view : NULL;
}
