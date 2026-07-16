#ifndef SEMANTIC_RESOLUTION_WORK_TREE_H
#define SEMANTIC_RESOLUTION_WORK_TREE_H

#include "../parser/node_fwd.h"

typedef struct arena_context_t arena_context_t;
typedef struct psx_resolution_work_tree_t psx_resolution_work_tree_t;

typedef enum {
  PSX_RESOLUTION_WORK_INVALID = 0,
  PSX_RESOLUTION_WORK_CLONED,
  PSX_RESOLUTION_WORK_BOUND,
  PSX_RESOLUTION_WORK_TYPED,
  PSX_RESOLUTION_WORK_LOWERED,
  PSX_RESOLUTION_WORK_FINALIZED,
} psx_resolution_work_phase_t;

psx_resolution_work_tree_t *psx_resolution_work_tree_create_from_syntax(
    arena_context_t *arena_context, const node_t *syntax_root);
const node_t *psx_resolution_work_tree_syntax_root(
    const psx_resolution_work_tree_t *tree);
node_t *psx_resolution_work_tree_mutable_semantic_root(
    psx_resolution_work_tree_t *tree);
const node_t *psx_resolution_work_tree_semantic_root(
    const psx_resolution_work_tree_t *tree);
node_t *psx_resolution_work_tree_legacy_root(
    psx_resolution_work_tree_t *tree);
psx_resolution_work_phase_t psx_resolution_work_tree_phase(
    const psx_resolution_work_tree_t *tree);
int psx_resolution_work_tree_advance_with_root(
    psx_resolution_work_tree_t *tree,
    psx_resolution_work_phase_t expected,
    psx_resolution_work_phase_t next, node_t *root);

#endif
