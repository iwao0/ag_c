#ifndef SEMANTIC_SYNTAX_TYPED_HIR_RESOLUTION_H
#define SEMANTIC_SYNTAX_TYPED_HIR_RESOLUTION_H

typedef struct node_t node_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_resolved_hir_build_failure_t
    psx_resolved_hir_build_failure_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_typed_hir_tree_t psx_typed_hir_tree_t;

typedef enum {
  PSX_SYNTAX_TYPED_HIR_NOT_HANDLED = 0,
  PSX_SYNTAX_TYPED_HIR_RESOLVED,
  PSX_SYNTAX_TYPED_HIR_FAILED,
} psx_syntax_typed_hir_resolution_status_t;

psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_expression_direct_to_typed_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *syntax_expression,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure);

#endif
