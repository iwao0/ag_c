#ifndef SEMANTIC_RESOLUTION_WORK_TREE_H
#define SEMANTIC_RESOLUTION_WORK_TREE_H

typedef struct psx_resolution_work_tree_t psx_resolution_work_tree_t;
typedef struct psx_typed_hir_tree_t psx_typed_hir_tree_t;

typedef enum {
  PSX_RESOLUTION_WORK_INVALID = 0,
  PSX_RESOLUTION_WORK_CLONED,
  PSX_RESOLUTION_WORK_BOUND,
  PSX_RESOLUTION_WORK_TYPED,
  PSX_RESOLUTION_WORK_LOWERED,
  PSX_RESOLUTION_WORK_FINALIZED,
  PSX_RESOLUTION_WORK_SEMANTIC_READY,
  PSX_RESOLUTION_WORK_HIR_READY,
} psx_resolution_work_phase_t;

const psx_typed_hir_tree_t *psx_resolution_work_tree_typed_hir(
    const psx_resolution_work_tree_t *tree);
psx_resolution_work_phase_t psx_resolution_work_tree_phase(
    const psx_resolution_work_tree_t *tree);

#endif
