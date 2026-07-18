#ifndef SEMANTIC_SYNTAX_TYPED_HIR_RESOLUTION_H
#define SEMANTIC_SYNTAX_TYPED_HIR_RESOLUTION_H

#include "typed_hir_build_status.h"

typedef struct node_t node_t;
typedef struct ag_compilation_options_t ag_compilation_options_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_local_lookup_point_t psx_local_lookup_point_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct psx_parsed_function_definition_t
    psx_parsed_function_definition_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_typed_hir_tree_t psx_typed_hir_tree_t;

typedef enum {
  PSX_SYNTAX_TYPED_HIR_REJECTED = 0,
  PSX_SYNTAX_TYPED_HIR_RESOLVED,
  PSX_SYNTAX_TYPED_HIR_FAILED,
} psx_syntax_typed_hir_resolution_status_t;

typedef struct {
  long long value;
  unsigned char is_constant;
} psx_syntax_integer_constant_result_t;

psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_expression_direct_to_typed_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *syntax_expression,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure);
psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_local_lookup_point_t *lookup_point,
    const node_t *syntax_expression,
    const psx_typed_hir_tree_t **typed_hir,
    psx_syntax_integer_constant_result_t *constant_result,
    psx_resolved_hir_build_failure_t *failure);
psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_expression_direct_to_typed_hir_with_lowering_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_expression,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure);

psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_initializer_direct_to_typed_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *syntax_initializer,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure);
psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_initializer_direct_to_typed_hir_with_lowering_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_initializer,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure);

psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_statement_direct_to_typed_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *syntax_statement,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure);

psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_function_direct_to_typed_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const psx_parsed_function_definition_t *syntax_function,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure);

#endif
