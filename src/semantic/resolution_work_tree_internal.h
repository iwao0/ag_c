#ifndef SEMANTIC_RESOLUTION_WORK_TREE_INTERNAL_H
#define SEMANTIC_RESOLUTION_WORK_TREE_INTERNAL_H

#include "resolution_work_tree.h"
#include "../parser/node_fwd.h"

typedef struct psx_resolved_hir_build_failure_t
    psx_resolved_hir_build_failure_t;
typedef struct psx_resolution_store_t psx_resolution_store_t;

psx_resolution_work_tree_t *psx_resolution_work_tree_create_from_syntax(
    psx_resolution_store_t *resolution_store,
    arena_context_t *arena_context, const node_t *syntax_root);
node_t *psx_resolution_work_tree_compatibility_root_mut(
    psx_resolution_work_tree_t *tree);
int psx_resolution_work_tree_replace_compatibility_root(
    psx_resolution_work_tree_t *tree, node_t *root);
int psx_resolution_work_tree_advance(
    psx_resolution_work_tree_t *tree,
    psx_resolution_work_phase_t expected,
    psx_resolution_work_phase_t next);
int psx_resolution_work_tree_materialize_typed_hir(
    psx_resolution_work_tree_t *tree,
    const struct psx_semantic_context_t *semantic_context,
    psx_resolved_hir_build_failure_t *failure);

#endif
