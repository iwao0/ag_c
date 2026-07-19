#ifndef SEMANTIC_SEMANTIC_TREE_RESOLUTION_H
#define SEMANTIC_SEMANTIC_TREE_RESOLUTION_H

typedef struct ag_compilation_options_t ag_compilation_options_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct psx_parsed_function_definition_t
    psx_parsed_function_definition_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_typed_hir_tree_t psx_typed_hir_tree_t;
typedef struct node_t node_t;
typedef struct token_t token_t;

const psx_typed_hir_tree_t *
psx_resolve_parsed_function_typed_hir_from_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok);
const psx_typed_hir_tree_t *
psx_resolve_expression_typed_hir_from_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok);
const psx_typed_hir_tree_t *
psx_resolve_initializer_typed_hir_from_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_initializer,
    const token_t *fallback_diag_tok);

#endif
