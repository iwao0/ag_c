#include "enum_constant_resolution.h"

#include "../parser/decl.h"
#include "../parser/function_public.h"
#include "../parser/gvar_public.h"
#include "../parser/local_registry.h"
#include "../parser/enum_const.h"
#include "../parser/diag.h"
#include "../parser/semantic_ctx.h"
#include "../parser/global_registry.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "syntax_typed_hir_resolution.h"
#include "typed_hir_materialization.h"

#include <string.h>
#include <limits.h>

void psx_resolve_enum_constant(
    const psx_enum_constant_resolution_request_t *request,
    psx_enum_constant_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_ENUM_CONSTANT_INVALID;
  if (!request || !request->semantic_context || !request->global_registry ||
      !request->local_registry || !request->name || request->name_len <= 0) {
    return;
  }
  psx_semantic_context_t *semantic_context = request->semantic_context;
  psx_local_registry_t *local_registry = request->local_registry;
  psx_global_registry_t *global_registry = request->global_registry;

  int scope_depth = ps_ctx_current_tag_scope_depth_in(semantic_context);
  if (ps_ctx_has_typedef_in_current_scope_in(
          semantic_context, request->name, request->name_len)) {
    resolution->status = PSX_ENUM_CONSTANT_TYPEDEF_NAME_CONFLICT;
    return;
  }
  if (scope_depth == 0) {
    if (ps_find_global_var_in(
            global_registry, request->name, request->name_len)) {
      resolution->status = PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT;
      return;
    }
    if (ps_ctx_has_function_name_in(
            semantic_context, request->name, request->name_len)) {
      resolution->status = PSX_ENUM_CONSTANT_FUNCTION_NAME_CONFLICT;
      return;
    }
  } else {
    lvar_t *local = ps_decl_find_lvar_in(
        local_registry, request->name, request->name_len);
    if (local && ps_lvar_registry_view(local).scope_seq ==
                     ps_local_registry_current_scope_seq_in(local_registry)) {
      resolution->status = PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT;
      return;
    }
  }

  if (!ps_ctx_register_enum_const_in_contexts(
          semantic_context, local_registry,
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
  psx_local_lookup_point_t lookup_point =
      ps_local_registry_capture_lookup_point_in(local_registry);
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
