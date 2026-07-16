#include "assignment_validation.h"

#include "../parser/ast.h"
#include "../parser/diag.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "function_call_resolution.h"

void psx_validate_assignment_in_context(
    psx_semantic_context_t *semantic_context, const node_t *node,
    ag_diagnostic_context_t *diagnostics,
    const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_ASSIGN || !node->lhs || !node->rhs) return;
  token_t *tok = node->tok ? node->tok : (token_t *)fallback_diag_tok;

  const psx_type_t *rhs_type = ps_node_get_type(node->rhs);
  if (rhs_type && rhs_type->kind == PSX_TYPE_VOID) {
    if (node->rhs->kind == ND_FUNCALL) {
      node_function_call_t *call =
          (node_function_call_t *)node->rhs;
      const char *direct_name =
          psx_function_call_direct_name(call);
      if (!call->callee && direct_name) {
        ps_diag_ctx_in(
            diagnostics, tok, "assign",
            "void 戻り値関数の結果は代入/初期化に使えません: '%.*s' (C11 6.5.16)",
            psx_function_call_direct_name_length(call), direct_name);
      }
    }
    ps_diag_ctx_in(
        diagnostics, tok, "assign",
        "void 戻り値関数の結果は代入/初期化に使えません (C11 6.5.16)");
  }

  if (node->is_decl_initializer) {
    const psx_type_t *lhs_type = ps_node_get_type(node->lhs);
    int lhs_is_pointer = lhs_type && ps_type_is_pointer(lhs_type);
    ps_node_reject_const_qual_discard_at_in(
        semantic_context, diagnostics, node->lhs, node->rhs, tok);
    if (lhs_is_pointer && node->rhs->kind == ND_NUM &&
        ((node_num_t *)node->rhs)->val != 0) {
      ps_diag_ctx_in(
          diagnostics, tok, "init",
          "ポインタ変数を非ゼロ整数定数 (%lld) で初期化できません (C11 6.5.16.1)",
          ((node_num_t *)node->rhs)->val);
    }
    if (!lhs_is_pointer && lhs_type &&
        !ps_type_is_tag_aggregate(lhs_type) &&
        lhs_type->kind != PSX_TYPE_ARRAY) {
      if (ps_node_value_is_pointer_like(node->rhs)) {
        ps_diag_ctx_in(
            diagnostics, tok, "init",
            "スカラ変数をポインタ型で初期化できません (C11 6.5.16.1)");
      }
      if (ps_type_is_tag_aggregate(rhs_type)) {
        ps_diag_ctx_in(
            diagnostics, tok, "init",
            "スカラ変数を %s 値で初期化できません (C11 6.5.16.1)",
            ps_ctx_tag_kind_spelling(
                ps_type_tag_token_kind(rhs_type)));
      }
    }
  }

  if (!node->is_source_assignment &&
      !node->is_source_compound_assignment) return;
  if (node->lhs->kind == ND_FUNCREF) {
    ps_diag_ctx_in(
        diagnostics, tok, "assign",
        "関数識別子に代入することはできません (C11 6.5.16p2)");
  }
  ps_node_expect_lvalue_at_in(diagnostics, node->lhs, "=", tok);
  ps_node_reject_const_assign_at_in(
      semantic_context, diagnostics, node->lhs, "=", tok);
  if (node->is_source_assignment) {
    ps_node_reject_const_qual_discard_at_in(
        semantic_context, diagnostics, node->lhs, node->rhs, tok);
  }
}
