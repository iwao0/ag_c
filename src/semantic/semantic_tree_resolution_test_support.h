#ifndef SEMANTIC_SEMANTIC_TREE_RESOLUTION_TEST_SUPPORT_H
#define SEMANTIC_SEMANTIC_TREE_RESOLUTION_TEST_SUPPORT_H

typedef struct ag_compilation_options_t ag_compilation_options_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct psx_parsed_function_definition_t
    psx_parsed_function_definition_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
typedef struct psx_resolution_work_tree_t psx_resolution_work_tree_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_typed_hir_tree_t psx_typed_hir_tree_t;
typedef struct node_t node_t;
typedef struct token_t token_t;

typedef struct {
  const psx_typed_hir_tree_t *typed_hir;
  node_t *compatibility_root;
} psx_function_compatibility_test_result_t;

int psx_resolve_parsed_function_compatibility_for_test_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok,
    psx_function_compatibility_test_result_t *result);

#endif
