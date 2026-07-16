#ifndef SEMANTIC_RESOLUTION_WORK_TREE_INTERNAL_H
#define SEMANTIC_RESOLUTION_WORK_TREE_INTERNAL_H

#include "resolution_work_tree.h"
#include "semantic_tree.h"
#include "../parser/node_fwd.h"

typedef struct psx_resolved_hir_build_failure_t
    psx_resolved_hir_build_failure_t;

psx_resolution_work_tree_t *psx_resolution_work_tree_create_from_syntax(
    arena_context_t *arena_context, const node_t *syntax_root);
psx_semantic_tree_t *psx_resolution_work_tree_semantic_tree_mut(
    psx_resolution_work_tree_t *tree);
const psx_semantic_tree_t *psx_resolution_work_tree_semantic_tree(
    const psx_resolution_work_tree_t *tree);
node_t *psx_resolution_work_tree_export_compatibility_ast(
    psx_resolution_work_tree_t *tree);
int psx_resolution_work_tree_advance(
    psx_resolution_work_tree_t *tree,
    psx_resolution_work_phase_t expected,
    psx_resolution_work_phase_t next);
int psx_resolution_work_tree_attach_typed_hir(
    psx_resolution_work_tree_t *tree,
    psx_typed_hir_tree_t *typed_hir);
int psx_resolution_work_tree_build_typed_hir(
    psx_resolution_work_tree_t *tree,
    const struct psx_semantic_context_t *semantic_context,
    psx_resolved_hir_build_failure_t *failure);

#endif
