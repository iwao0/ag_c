#ifndef SEMANTIC_RESOLUTION_WORK_TREE_H
#define SEMANTIC_RESOLUTION_WORK_TREE_H

#include "../parser/node_fwd.h"
#include "resolved_tree.h"

typedef struct arena_context_t arena_context_t;

psx_resolved_tree_t *psx_resolved_tree_create_from_syntax(
    arena_context_t *arena_context, const node_t *syntax_root);
node_t *psx_resolved_tree_mutable_root(psx_resolved_tree_t *tree);
const node_t *psx_resolved_tree_root(const psx_resolved_tree_t *tree);
node_t *psx_resolved_tree_legacy_root(psx_resolved_tree_t *tree);
int psx_resolved_tree_advance_with_root(
    psx_resolved_tree_t *tree, psx_resolved_tree_phase_t expected,
    psx_resolved_tree_phase_t next, node_t *root);

#endif
