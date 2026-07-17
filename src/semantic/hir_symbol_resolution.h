#ifndef SEMANTIC_HIR_SYMBOL_RESOLUTION_H
#define SEMANTIC_HIR_SYMBOL_RESOLUTION_H

#include "../hir/hir_internal.h"

typedef struct global_var_t global_var_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;

int psx_resolve_global_hir_symbol_spec_in(
    const psx_semantic_context_t *semantic_context,
    const global_var_t *global,
    psx_hir_symbol_spec_t *symbol);

#endif
