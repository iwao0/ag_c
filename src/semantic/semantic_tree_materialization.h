#ifndef SEMANTIC_SEMANTIC_TREE_MATERIALIZATION_H
#define SEMANTIC_SEMANTIC_TREE_MATERIALIZATION_H

#include "typed_hir_materialization.h"
#include "../parser/node_fwd.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_semantic_tree_t psx_semantic_tree_t;

int psx_semantic_tree_materialize(
    psx_semantic_tree_t *semantic_tree,
    const node_t *resolution_root,
    const psx_semantic_context_t *semantic_context,
    psx_resolved_hir_build_failure_t *failure);

#endif
