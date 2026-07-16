#ifndef SEMANTIC_RESOLVED_TREE_HIR_H
#define SEMANTIC_RESOLVED_TREE_HIR_H

#include "../hir/hir.h"
#include "resolved_tree.h"

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

int psx_resolved_tree_materialize_hir(
    psx_resolved_tree_t *resolved_tree,
    const psx_semantic_context_t *semantic_context,
    psx_resolved_hir_build_failure_t *failure);
psx_hir_node_id_t psx_resolved_tree_emit_hir(
    psx_hir_module_t *module,
    const psx_resolved_tree_t *resolved_tree,
    psx_resolved_hir_build_failure_t *failure);

#endif
