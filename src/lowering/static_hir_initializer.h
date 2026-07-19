#ifndef LOWERING_STATIC_HIR_INITIALIZER_H
#define LOWERING_STATIC_HIR_INITIALIZER_H

#include "../hir/hir.h"
#include "../type_system/type_ids.h"

typedef struct global_var_t global_var_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct psx_static_aggregate_initializer_plan_t
    psx_static_aggregate_initializer_plan_t;
typedef struct token_t token_t;

int psx_lower_static_scalar_hir_initializer(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context,
    global_var_t *global, psx_type_id_t type_id,
    const psx_hir_module_t *hir, psx_hir_node_id_t root);
int psx_build_static_aggregate_hir_initializer_plan(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context,
    psx_type_id_t type_id, const psx_hir_module_t *hir,
    psx_hir_node_id_t root, token_t *fallback_tok,
    psx_static_aggregate_initializer_plan_t *plan);

#endif
