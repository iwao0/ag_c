#include "semantic_pipeline.h"
#include "semantic_pipeline_internal.h"

#include "../lowering/runtime_context.h"
#include "../lowering/static_hir_initializer.h"
#include "../parser/semantic_ctx.h"
#include "../semantic/semantic_tree_resolution.h"
#include "../semantic/continuation_syntax_validation.h"

int psx_frontend_resolve_parsed_function_to_hir_in_session(
    ag_compilation_session_t *session,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok,
    psx_hir_node_id_t *hir_root) {
  if (hir_root) *hir_root = PSX_HIR_NODE_ID_INVALID;
  if (!ag_compilation_session_is_complete(session) ||
      !syntax_function || !syntax_function->body || !hir_root)
    return 0;
  if (!psx_validate_continuation_condition_types_in_contexts(
          ag_compilation_session_semantic_context(session),
          ag_compilation_session_continuation(session),
          syntax_function))
    return 0;
  return psx_resolve_parsed_function_hir_from_syntax_in_contexts(
      ag_compilation_session_semantic_context(session),
      ag_compilation_session_global_registry(session),
      ag_compilation_session_local_registry(session),
      ag_compilation_session_lowering_context(session),
      ag_compilation_session_options_view(session),
      syntax_function, fallback_diag_tok,
      ag_compilation_session_hir_module(session), hir_root);
}

int psx_frontend_resolve_expression_to_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok,
    psx_frontend_expression_hir_t *result) {
  if (result) {
    *result = (psx_frontend_expression_hir_t){
        .root = PSX_HIR_NODE_ID_INVALID,
    };
  }
  if (!result) return 0;
  psx_hir_module_t *module = psx_hir_module_create();
  if (!module) return 0;
  psx_hir_node_id_t root = PSX_HIR_NODE_ID_INVALID;
  if (!psx_resolve_expression_hir_from_syntax_in_contexts(
          semantic_context, global_registry, local_registry,
          lowering_context, options, syntax_expression,
          fallback_diag_tok, module, &root)) {
    psx_hir_module_destroy(module);
    return 0;
  }
  result->module = module;
  result->root = root;
  return 1;
}

void psx_frontend_expression_hir_dispose(
    psx_frontend_expression_hir_t *expression) {
  if (!expression) return;
  psx_hir_module_destroy(expression->module);
  *expression = (psx_frontend_expression_hir_t){
      .root = PSX_HIR_NODE_ID_INVALID,
  };
}

int psx_frontend_resolve_static_aggregate_initializer_plan_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    psx_qual_type_t type, const node_t *syntax,
    const token_t *fallback_diag_tok,
    psx_static_aggregate_initializer_plan_t *plan) {
  if (plan) *plan = (psx_static_aggregate_initializer_plan_t){0};
  if (type.type_id == PSX_TYPE_ID_INVALID || !plan) return 0;
  psx_hir_module_t *hir = psx_hir_module_create();
  if (!hir) return 0;
  psx_hir_node_id_t root = PSX_HIR_NODE_ID_INVALID;
  int resolved = psx_resolve_initializer_hir_from_syntax_in_contexts(
      semantic_context, global_registry, local_registry,
      lowering_context, options, syntax, fallback_diag_tok,
      hir, &root);
  int built = resolved &&
              psx_build_static_aggregate_hir_initializer_plan(
                  global_registry, lowering_context,
                  type.type_id,
                  hir, root, (token_t *)fallback_diag_tok, plan);
  psx_hir_module_destroy(hir);
  return built;
}
