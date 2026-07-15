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

#include <string.h>

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

long long psx_resolve_prepared_enum_const_expr_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_parsed_enum_expr_t *expression) {
  if (!expression) return 0;
  if (expression->kind == PSX_ENUM_EXPR_VALUE) return expression->value;
  if (expression->kind == PSX_ENUM_EXPR_IDENTIFIER) {
    long long value = 0;
    if (!ps_ctx_find_enum_const_in(
            semantic_context,
            expression->identifier, expression->identifier_len, &value)) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(semantic_context),
          expression->diagnostic_token, "enum",
          diag_message_for_in(
              ps_ctx_diagnostics(semantic_context),
              DIAG_ERR_PARSER_ENUM_CONST_UNDEFINED),
          expression->identifier_len, expression->identifier);
    }
    return value;
  }
  long long left =
      psx_resolve_prepared_enum_const_expr_in_context(
          semantic_context, expression->lhs);
  if (expression->kind == PSX_ENUM_EXPR_CONDITIONAL) {
    return left
               ? psx_resolve_prepared_enum_const_expr_in_context(
                     semantic_context, expression->rhs)
               : psx_resolve_prepared_enum_const_expr_in_context(
                     semantic_context, expression->alternative);
  }
  if (expression->kind == PSX_ENUM_EXPR_UNARY) {
    switch (expression->op) {
      case TK_PLUS: return left;
      case TK_MINUS: return -left;
      case TK_TILDE: return ~left;
      case TK_BANG: return !left;
      default: return 0;
    }
  }
  long long right =
      psx_resolve_prepared_enum_const_expr_in_context(
          semantic_context, expression->rhs);
  switch (expression->op) {
    case TK_PLUS: return left + right;
    case TK_MINUS: return left - right;
    case TK_MUL: return left * right;
    case TK_DIV: return left / right;
    case TK_MOD: return left % right;
    case TK_SHL: return left << right;
    case TK_SHR: return left >> right;
    case TK_LT: return left < right;
    case TK_LE: return left <= right;
    case TK_GT: return left > right;
    case TK_GE: return left >= right;
    case TK_EQEQ: return left == right;
    case TK_NEQ: return left != right;
    case TK_AMP: return left & right;
    case TK_CARET: return left ^ right;
    case TK_PIPE: return left | right;
    case TK_ANDAND: return left && right;
    case TK_OROR: return left || right;
    default: return 0;
  }
}
