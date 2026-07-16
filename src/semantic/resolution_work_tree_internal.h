#ifndef SEMANTIC_RESOLUTION_WORK_TREE_INTERNAL_H
#define SEMANTIC_RESOLUTION_WORK_TREE_INTERNAL_H

#include "resolution_work_tree.h"

psx_resolution_work_tree_t *psx_resolution_work_tree_create_from_syntax(
    arena_context_t *arena_context, const node_t *syntax_root);
node_t *psx_resolution_work_tree_mutable_semantic_root(
    psx_resolution_work_tree_t *tree);
const node_t *psx_resolution_work_tree_semantic_root(
    const psx_resolution_work_tree_t *tree);
node_t *psx_resolution_work_tree_legacy_root(
    psx_resolution_work_tree_t *tree);
int psx_resolution_work_tree_advance_with_root(
    psx_resolution_work_tree_t *tree,
    psx_resolution_work_phase_t expected,
    psx_resolution_work_phase_t next, node_t *root);
int psx_resolution_work_tree_attach_typed_hir(
    psx_resolution_work_tree_t *tree,
    psx_typed_hir_tree_t *typed_hir);

#endif
