#ifndef SEMANTIC_HIR_LOCAL_RESOLUTION_H
#define SEMANTIC_HIR_LOCAL_RESOLUTION_H

#include "../hir/hir_internal.h"

typedef struct lvar_t lvar_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;

int psx_resolve_local_hir_node_spec_in(
    const psx_semantic_context_t *semantic_context,
    const lvar_t *local, int storage_offset,
    psx_hir_node_spec_t *spec);

int psx_apply_local_vla_hir_node_spec_in(
    const psx_semantic_context_t *semantic_context,
    const lvar_t *local, psx_hir_node_spec_t *spec);

#endif
