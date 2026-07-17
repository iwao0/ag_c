#ifndef SEMANTIC_SEMANTIC_TREE_RESOLUTION_INTERNAL_H
#define SEMANTIC_SEMANTIC_TREE_RESOLUTION_INTERNAL_H

typedef struct ag_compilation_options_t ag_compilation_options_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct psx_resolution_work_tree_t psx_resolution_work_tree_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct token_t token_t;

int psx_resolve_expression_compatibility_work_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    psx_resolution_work_tree_t *work_tree,
    const token_t *fallback_diag_tok);

#endif
