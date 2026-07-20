#include "enum_constant_resolution.h"

#include "scope_graph.h"
#include "syntax_typed_hir_resolution.h"
#include "typed_hir_materialization.h"

#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../parser/diag.h"
#include "../parser/enum_const.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../parser/semantic_ctx.h"

#include <limits.h>
#include <string.h>

void psx_resolve_enum_constant(
    const psx_enum_constant_resolution_request_t *request,
    psx_enum_constant_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_ENUM_CONSTANT_INVALID;
  if (!request || !request->semantic_context || !request->name ||
      request->name_len <= 0) {
    return;
  }
  psx_semantic_context_t *semantic_context = request->semantic_context;
  psx_scope_graph_t *scope_graph = ps_ctx_scope_graph(semantic_context);
  if (!scope_graph) return;

  int scope_depth = ps_ctx_current_tag_scope_depth_in(semantic_context);
  const psx_scope_declaration_t *existing =
      psx_scope_graph_lookup_declaration_in_scope(
          scope_graph, psx_scope_graph_current_scope(scope_graph),
          PSX_NAMESPACE_ORDINARY, request->name, request->name_len);
  if (existing) {
    switch (existing->kind) {
    case PSX_DECL_TYPEDEF:
      resolution->status = PSX_ENUM_CONSTANT_TYPEDEF_NAME_CONFLICT;
      return;
    case PSX_DECL_LOCAL_OBJECT:
    case PSX_DECL_GLOBAL_OBJECT:
      resolution->status = PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT;
      return;
    case PSX_DECL_FUNCTION:
      resolution->status = PSX_ENUM_CONSTANT_FUNCTION_NAME_CONFLICT;
      return;
    case PSX_DECL_ENUM_CONSTANT:
      resolution->status = PSX_ENUM_CONSTANT_DUPLICATE;
      return;
    default:
      resolution->status = PSX_ENUM_CONSTANT_INVALID;
      return;
    }
  }

  if (!ps_ctx_register_enum_const_in(
          semantic_context,
          request->name, request->name_len, request->value,
          &resolution->created)) {
    resolution->status = PSX_ENUM_CONSTANT_DUPLICATE;
    return;
  }
  resolution->scope_depth = scope_depth;
  resolution->status = PSX_ENUM_CONSTANT_OK;
}

int psx_resolve_enum_initializer_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *syntax_expression,
    const token_t *diagnostic_token,
    long long *value) {
  if (value) *value = 0;
  if (!semantic_context || !global_registry || !local_registry ||
      !syntax_expression || !value)
    return 0;
  const psx_typed_hir_tree_t *typed_hir = NULL;
  psx_scope_lookup_point_t lookup_point =
      psx_scope_graph_capture_lookup_point(
          ps_ctx_scope_graph(semantic_context));
  psx_syntax_integer_constant_result_t constant_result;
  psx_resolved_hir_build_failure_t failure;
  psx_syntax_typed_hir_resolution_status_t status =
      psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts(
          semantic_context, global_registry, local_registry, &lookup_point,
          syntax_expression, &typed_hir, &constant_result, &failure);
  if (status == PSX_SYNTAX_TYPED_HIR_RESOLVED && typed_hir &&
      constant_result.is_constant) {
    if (constant_result.value < INT_MIN ||
        constant_result.value > INT_MAX) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(semantic_context),
          (token_t *)diagnostic_token, "enum",
          "enumerator value is not representable as int");
      return 0;
    }
    *value = constant_result.value;
    return 1;
  }
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  if (status == PSX_SYNTAX_TYPED_HIR_FAILED) {
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: enum initializer direct resolution failed "
        "(status %d, node kind %d)",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
        (int)failure.status, failure.source_node_kind);
    return 0;
  }
  ps_diag_ctx_in(
      diagnostics, (token_t *)diagnostic_token, "enum",
      "enumerator value is not an integer constant expression");
  return 0;
}
