#ifndef FRONTEND_SEMANTIC_PIPELINE_INTERNAL_H
#define FRONTEND_SEMANTIC_PIPELINE_INTERNAL_H

#include "semantic_pipeline.h"
#include "../lowering/static_initializer_plan.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct psx_resolution_work_tree_t psx_resolution_work_tree_t;

typedef struct {
  psx_hir_module_t *module;
  psx_hir_node_id_t root;
} psx_frontend_expression_hir_t;

psx_resolution_work_tree_t *
psx_frontend_resolve_function_work_tree_in_session(
    ag_compilation_session_t *session,
    const node_t *syntax_function, const token_t *fallback_diag_tok,
    psx_hir_node_id_t *hir_root);
int psx_frontend_resolve_expression_to_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok,
    psx_frontend_expression_hir_t *result);
void psx_frontend_expression_hir_dispose(
    psx_frontend_expression_hir_t *expression);
int psx_frontend_resolve_static_aggregate_initializer_plan_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const psx_type_t *type, const node_t *syntax,
    const token_t *fallback_diag_tok,
    psx_static_aggregate_initializer_plan_t *plan);
node_t *psx_frontend_analyze_expression_in_session(
    ag_compilation_session_t *session,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok);
node_t *psx_frontend_analyze_expression_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok);
node_t *psx_frontend_analyze_initializer_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax, const token_t *fallback_diag_tok);

#endif
