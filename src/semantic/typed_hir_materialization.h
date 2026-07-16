#ifndef SEMANTIC_TYPED_HIR_MATERIALIZATION_H
#define SEMANTIC_TYPED_HIR_MATERIALIZATION_H

#include "../hir/hir.h"
#include "resolution_work_tree.h"
#include "typed_hir_tree.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef enum {
  PSX_RESOLVED_HIR_BUILD_OK = 0,
  PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
  PSX_RESOLVED_HIR_BUILD_UNFINALIZED_RESOLUTION,
  PSX_RESOLVED_HIR_BUILD_UNMATERIALIZED,
  PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS,
  PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE,
  PSX_RESOLVED_HIR_BUILD_MISSING_RESOLVED_SYMBOL,
  PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
} psx_resolved_hir_build_status_t;

typedef struct {
  psx_resolved_hir_build_status_t status;
  int source_node_kind;
} psx_resolved_hir_build_failure_t;

psx_typed_hir_tree_t *psx_resolution_work_tree_materialize_hir(
    const psx_resolution_work_tree_t *work_tree,
    const psx_semantic_context_t *semantic_context,
    psx_resolved_hir_build_failure_t *failure);
psx_hir_node_id_t psx_typed_hir_tree_emit(
    psx_hir_module_t *module,
    const psx_typed_hir_tree_t *typed_tree,
    psx_resolved_hir_build_failure_t *failure);

#endif
