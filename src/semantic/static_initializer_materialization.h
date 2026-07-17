#ifndef SEMANTIC_STATIC_INITIALIZER_MATERIALIZATION_H
#define SEMANTIC_STATIC_INITIALIZER_MATERIALIZATION_H

typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct psx_static_aggregate_initializer_plan_t
    psx_static_aggregate_initializer_plan_t;
typedef struct psx_typed_hir_tree_t psx_typed_hir_tree_t;
typedef struct psx_type_t psx_type_t;
typedef struct token_t token_t;

int psx_materialize_static_aggregate_initializer_plan(
    const psx_typed_hir_tree_t *typed_tree,
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context,
    const psx_type_t *type, token_t *diag_tok,
    psx_static_aggregate_initializer_plan_t *plan);

#endif
