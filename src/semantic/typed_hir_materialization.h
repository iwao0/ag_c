#ifndef SEMANTIC_TYPED_HIR_MATERIALIZATION_H
#define SEMANTIC_TYPED_HIR_MATERIALIZATION_H

#include "../hir/hir.h"
#include "typed_hir_build_status.h"
#include "typed_hir_tree.h"

psx_hir_node_id_t psx_typed_hir_tree_emit(
    psx_hir_module_t *module,
    const psx_typed_hir_tree_t *typed_tree,
    psx_resolved_hir_build_failure_t *failure);

#endif
