#include "parser_compatibility_test_hook.h"

#include "../../src/frontend/translation_unit_resolver.h"
#include "../../src/semantic/semantic_tree_resolution_test_support.h"
#include "../../src/semantic/typed_hir_materialization.h"

typedef struct {
  node_t **compatibility_root;
} compatibility_resolver_context_t;

static int resolve_function_for_compatibility_test(
    void *context, ag_compilation_session_t *session,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok,
    psx_hir_node_id_t *hir_root) {
  compatibility_resolver_context_t *resolver_context = context;
  psx_function_compatibility_test_result_t resolution;
  if (!resolver_context || !resolver_context->compatibility_root ||
      !hir_root ||
      !psx_resolve_parsed_function_compatibility_for_test_in_contexts(
          ag_compilation_session_semantic_context(session),
          ag_compilation_session_global_registry(session),
          ag_compilation_session_local_registry(session),
          ag_compilation_session_parser_runtime_context(session),
          ag_compilation_session_lowering_context(session),
          ag_compilation_session_options_view(session),
          syntax_function, fallback_diag_tok, &resolution))
    return 0;
  psx_resolved_hir_build_failure_t failure;
  *hir_root = psx_typed_hir_tree_emit(
      ag_compilation_session_hir_module(session),
      resolution.typed_hir, &failure);
  if (*hir_root == PSX_HIR_NODE_ID_INVALID) return 0;
  *resolver_context->compatibility_root =
      resolution.compatibility_root;
  return 1;
}

int psx_test_frontend_next_function_compatibility_tree(
    psx_frontend_stream_t *stream,
    psx_frontend_function_t *function,
    node_t **compatibility_root) {
  if (compatibility_root) *compatibility_root = NULL;
  if (!compatibility_root) return 0;
  compatibility_resolver_context_t context = {
      .compatibility_root = compatibility_root,
  };
  return psx_frontend_next_function_with_resolver(
      stream, function, resolve_function_for_compatibility_test,
      &context);
}
