#ifndef SEMANTIC_TYPED_HIR_TREE_MATERIALIZATION_H
#define SEMANTIC_TYPED_HIR_TREE_MATERIALIZATION_H

#include "typed_hir_materialization.h"
#include "../parser/node_fwd.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

psx_typed_hir_tree_t *psx_typed_hir_tree_materialize(
    const node_t *resolution_root,
    const psx_semantic_context_t *semantic_context,
    psx_resolved_hir_build_failure_t *failure);

#endif
