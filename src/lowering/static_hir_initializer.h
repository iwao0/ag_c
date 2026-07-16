#ifndef LOWERING_STATIC_HIR_INITIALIZER_H
#define LOWERING_STATIC_HIR_INITIALIZER_H

#include "../hir/hir.h"

typedef struct global_var_t global_var_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct psx_type_t psx_type_t;

int psx_lower_static_scalar_hir_initializer(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context,
    global_var_t *global, const psx_type_t *type,
    const psx_hir_module_t *hir, psx_hir_node_id_t root);

#endif
