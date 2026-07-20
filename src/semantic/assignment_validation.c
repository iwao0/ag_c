#include "assignment_validation.h"
#include "assignment_resolution.h"
#include "resolved_node_kind.h"

#include "../diag/diag.h"
#include "../parser/ast.h"
#include "../parser/diag.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "function_call_resolution.h"
#include "resolved_object_ref.h"

static psx_qual_type_t assignment_node_qual_type(
    psx_semantic_context_t *semantic_context,
    const node_t *node) {
  if (!semantic_context || !node)
    return (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  return ps_node_qual_type(store, node);
}

static int assignment_compound_operator(
    token_kind_t source_operator,
    psx_compound_assignment_operator_t *operation) {
  if (!operation) return 0;
  switch (source_operator) {
    case TK_PLUSEQ: *operation = PSX_COMPOUND_ASSIGN_ADD; return 1;
    case TK_MINUSEQ: *operation = PSX_COMPOUND_ASSIGN_SUB; return 1;
    case TK_MULEQ: *operation = PSX_COMPOUND_ASSIGN_MUL; return 1;
    case TK_DIVEQ: *operation = PSX_COMPOUND_ASSIGN_DIV; return 1;
    case TK_MODEQ: *operation = PSX_COMPOUND_ASSIGN_MOD; return 1;
    case TK_SHLEQ: *operation = PSX_COMPOUND_ASSIGN_SHL; return 1;
    case TK_SHREQ: *operation = PSX_COMPOUND_ASSIGN_SHR; return 1;
    case TK_ANDEQ: *operation = PSX_COMPOUND_ASSIGN_BITAND; return 1;
    case TK_XOREQ: *operation = PSX_COMPOUND_ASSIGN_BITXOR; return 1;
    case TK_OREQ: *operation = PSX_COMPOUND_ASSIGN_BITOR; return 1;
    default: return 0;
  }
}

static int diagnose_assignment_resolution(
    psx_semantic_context_t *semantic_context,
    const node_t *node,
    const psx_assignment_types_resolution_t *resolution,
    ag_diagnostic_context_t *diagnostics, token_t *tok) {
  if (!semantic_context || !node || !resolution || !diagnostics)
    return 0;
  switch (resolution->status) {
    case PSX_ASSIGNMENT_TARGET_NOT_MODIFIABLE: {
      psx_qual_type_t target_type = assignment_node_qual_type(
          semantic_context, node->lhs);
      if ((target_type.qualifiers &
           PSX_TYPE_QUALIFIER_CONST) != 0) {
        ps_node_reject_const_assign_at_in(
            semantic_context, diagnostics, node->lhs, "=", tok);
        return 1;
      }
      diag_emit_tokf_in(
          diagnostics,
          DIAG_ERR_PARSER_ASSIGN_TARGET_NOT_MODIFIABLE, tok,
          "%s", diag_message_for_in(
                    diagnostics,
                    DIAG_ERR_PARSER_ASSIGN_TARGET_NOT_MODIFIABLE));
      return 1;
    }
    case PSX_ASSIGNMENT_DISCARDS_QUALIFIERS:
      ps_node_reject_const_qual_discard_at_in(
          semantic_context, diagnostics, node->lhs, node->rhs, tok);
      return 1;
    case PSX_ASSIGNMENT_TYPES_INCOMPATIBLE:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_ASSIGN_TYPES_INCOMPATIBLE,
          tok, "%s", diag_message_for_in(
                         diagnostics,
                         DIAG_ERR_PARSER_ASSIGN_TYPES_INCOMPATIBLE));
      return 1;
    default:
      return 0;
  }
}

void psx_validate_assignment_in_context(
    psx_semantic_context_t *semantic_context, const node_t *node,
    ag_diagnostic_context_t *diagnostics,
    const token_t *fallback_diag_tok) {
  if (!node ||
      (node->kind != ND_ASSIGN &&
       node->kind != ND_COMPOUND_ASSIGN) ||
      !node->lhs || !node->rhs)
    return;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  token_t *tok = node->tok ? node->tok : (token_t *)fallback_diag_tok;

  psx_type_shape_t rhs_type = {0};
  int has_rhs_type = ps_node_type_shape(store, node->rhs, &rhs_type);
  if (has_rhs_type && rhs_type.kind == PSX_TYPE_VOID) {
    if (node->rhs->kind == ND_FUNCALL) {
      node_function_call_t *call =
          (node_function_call_t *)node->rhs;
      const char *direct_name =
          psx_function_call_direct_name(store, call);
      if (!call->callee && direct_name) {
        ps_diag_ctx_in(
            diagnostics, tok, "assign",
            "void 戻り値関数の結果は代入/初期化に使えません: '%.*s' (C11 6.5.16)",
            psx_function_call_direct_name_length(store, call), direct_name);
      }
    }
    ps_diag_ctx_in(
        diagnostics, tok, "assign",
        "void 戻り値関数の結果は代入/初期化に使えません (C11 6.5.16)");
  }

  if (ps_node_is_decl_initializer(store, node)) {
    psx_type_shape_t lhs_type = {0};
    int has_lhs_type = ps_node_type_shape(store, node->lhs, &lhs_type);
    int lhs_is_pointer = has_lhs_type &&
                         lhs_type.kind == PSX_TYPE_POINTER;
    ps_node_reject_const_qual_discard_at_in(
        semantic_context, diagnostics, node->lhs, node->rhs, tok);
    if (lhs_is_pointer && node->rhs->kind == ND_NUM &&
        ((node_num_t *)node->rhs)->val != 0) {
      ps_diag_ctx_in(
          diagnostics, tok, "init",
          "ポインタ変数を非ゼロ整数定数 (%lld) で初期化できません (C11 6.5.16.1)",
          ((node_num_t *)node->rhs)->val);
    }
    if (!lhs_is_pointer && has_lhs_type &&
        !psx_type_kind_is_aggregate(lhs_type.kind) &&
        lhs_type.kind != PSX_TYPE_ARRAY) {
      if (ps_node_value_is_pointer_like(store, node->rhs)) {
        ps_diag_ctx_in(
            diagnostics, tok, "init",
            "スカラ変数をポインタ型で初期化できません (C11 6.5.16.1)");
      }
      if (has_rhs_type && psx_type_kind_is_aggregate(rhs_type.kind)) {
        ps_diag_ctx_in(
            diagnostics, tok, "init",
            "スカラ変数を %s 値で初期化できません (C11 6.5.16.1)",
            ps_ctx_tag_kind_spelling(
                rhs_type.kind == PSX_TYPE_STRUCT ? TK_STRUCT : TK_UNION));
      }
    }
  }

  if (!ps_node_is_source_assignment(store, node) &&
      node->kind != ND_COMPOUND_ASSIGN)
    return;
  if (psx_resolved_object_ref_node_kind(store, node->lhs) == ND_FUNCREF) {
    diag_emit_tokf_in(
        diagnostics, DIAG_ERR_PARSER_ASSIGN_FUNCTION_TARGET, tok,
        "%s", diag_message_for_in(
                  diagnostics,
                  DIAG_ERR_PARSER_ASSIGN_FUNCTION_TARGET));
    return;
  }
  if (ps_node_is_source_assignment(store, node)) {
    psx_assignment_types_resolution_t resolution;
    psx_resolve_assignment_qual_types_in(
        semantic_context,
        assignment_node_qual_type(semantic_context, node->lhs),
        assignment_node_qual_type(semantic_context, node->rhs),
        node->rhs->kind == ND_NUM &&
            ((const node_num_t *)node->rhs)->val == 0,
        &resolution);
    if (diagnose_assignment_resolution(
            semantic_context, node, &resolution,
            diagnostics, tok))
      return;
  } else if (node->kind == ND_COMPOUND_ASSIGN) {
    psx_compound_assignment_operator_t operation;
    psx_assignment_types_resolution_t resolution;
    if (assignment_compound_operator(node->source_op, &operation)) {
      psx_resolve_compound_assignment_qual_types_in(
          semantic_context, operation,
          assignment_node_qual_type(semantic_context, node->lhs),
          assignment_node_qual_type(semantic_context, node->rhs),
          &resolution);
      if (diagnose_assignment_resolution(
              semantic_context, node, &resolution,
              diagnostics, tok))
        return;
    }
  }
  ps_node_expect_lvalue_at_in(
      store, diagnostics, node->lhs, "=", tok);
}
