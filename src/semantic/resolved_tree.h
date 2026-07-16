#ifndef SEMANTIC_RESOLVED_TREE_H
#define SEMANTIC_RESOLVED_TREE_H

typedef struct psx_resolved_tree_t psx_resolved_tree_t;

typedef enum {
  PSX_RESOLVED_TREE_INVALID = 0,
  PSX_RESOLVED_TREE_CLONED,
  PSX_RESOLVED_TREE_BOUND,
  PSX_RESOLVED_TREE_TYPED,
  PSX_RESOLVED_TREE_LOWERED,
  PSX_RESOLVED_TREE_FINALIZED,
  PSX_RESOLVED_TREE_HIR_READY,
} psx_resolved_tree_phase_t;

psx_resolved_tree_phase_t psx_resolved_tree_phase(
    const psx_resolved_tree_t *tree);

#endif
