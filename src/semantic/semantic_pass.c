#include "semantic_pass.h"
#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/dynarray.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../diag/diag.h"
#include "../lowering/semantic_lowering_pass.h"
#include <string.h>

static void semantic_visit_node(node_t *node);
static void semantic_transform_node(node_t *node, node_func_t *current_func,
                                    const token_t *fallback_diag_tok);
static void semantic_warn_node(node_t *node, node_func_t *current_func,
                               const token_t *fallback_diag_tok);
static void semantic_validate_control_flow(node_t *node, const token_t *fallback_diag_tok,
                                           int loop_depth, int switch_depth);
static void semantic_validate_switch_labels(node_t *node, const token_t *fallback_diag_tok);
static void semantic_check_unreachable_in_node(node_t *node, const token_t *fallback_diag_tok);
static void semantic_collect_lvar_usage_events(node_t *node,
                                               psx_lvar_usage_region_t *inherited_region);

static void semantic_transform_initializer_syntax(
    node_t *syntax, node_func_t *current_func,
    const token_t *fallback_diag_tok) {
  if (!syntax) return;
  if (syntax->kind != ND_INIT_LIST) {
    semantic_transform_node(syntax, current_func, fallback_diag_tok);
    return;
  }
  node_init_list_t *list = (node_init_list_t *)syntax;
  for (int i = 0; i < list->entry_count; i++) {
    if (list->entries[i].designator_count > 0) {
      for (int d = 0; d < list->entries[i].designator_count; d++) {
        psx_initializer_designator_t *designator =
            &list->entries[i].designators[d];
        if (designator->kind == PSX_INIT_DESIGNATOR_INDEX) {
          semantic_transform_node(
              designator->index_expr, current_func, fallback_diag_tok);
          semantic_transform_node(
              designator->range_end_expr, current_func, fallback_diag_tok);
        }
      }
    } else {
      for (int d = 0; d < list->entries[i].index_expr_count; d++) {
        semantic_transform_node(
            list->entries[i].index_exprs[d], current_func, fallback_diag_tok);
      }
    }
    semantic_transform_initializer_syntax(
        list->entries[i].value, current_func, fallback_diag_tok);
  }
}

typedef struct {
  psx_type_t *type;
  tk_float_kind_t fp_kind;
  int is_void;
  int is_pointer;
  int aggregate_size;
} semantic_return_type_view_t;

static semantic_return_type_view_t semantic_return_type_view(node_func_t *fn) {
  semantic_return_type_view_t view = {0};
  view.type = ps_node_get_type((node_t *)fn);
  psx_type_t *type = view.type;
  if (!type) return view;
  view.fp_kind = ps_node_value_fp_kind((node_t *)fn);
  view.is_void = type->kind == PSX_TYPE_VOID;
  view.is_pointer = ps_type_is_pointer(type);
  view.aggregate_size = ps_node_aggregate_value_size((node_t *)fn);
  return view;
}

static void semantic_visit_node_array(node_t **nodes) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++) {
    semantic_visit_node(nodes[i]);
  }
}

static void semantic_visit_node(node_t *node) {
  if (!node) return;

  switch (node->kind) {
    case ND_BLOCK: {
      node_block_t *block = (node_block_t *)node;
      semantic_visit_node_array(block->body);
      break;
    }
    case ND_FUNCDEF: {
      node_func_t *fn = (node_func_t *)node;
      for (int i = 0; i < fn->nargs; i++) semantic_visit_node(fn->args[i]);
      semantic_visit_node(node->rhs);
      break;
    }
    case ND_FUNCALL: {
      node_func_t *fn = (node_func_t *)node;
      semantic_visit_node(fn->callee);
      for (int i = 0; i < fn->nargs; i++) semantic_visit_node(fn->args[i]);
      break;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *ctrl = (node_ctrl_t *)node;
      semantic_visit_node(ctrl->init);
      semantic_visit_node(node->lhs);
      semantic_visit_node(node->rhs);
      semantic_visit_node(ctrl->inc);
      semantic_visit_node(ctrl->els);
      break;
    }
    case ND_SWITCH:
    case ND_CASE:
    case ND_DEFAULT:
    case ND_WHILE:
    case ND_DO_WHILE:
    case ND_RETURN:
    case ND_LABEL:
    case ND_STMT_EXPR:
    default:
      semantic_visit_node(node->lhs);
      semantic_visit_node(node->rhs);
      break;
  }

  (void)ps_node_materialize_type(node);
}

static void semantic_transform_return(node_t *node, node_func_t *current_func,
                                      const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_RETURN || !current_func) return;
  const token_t *tok = node->tok ? node->tok : fallback_diag_tok;
  semantic_return_type_view_t ret = semantic_return_type_view(current_func);

  node->fp_kind = ret.fp_kind;

  if (!node->lhs) {
    if (!ret.is_void) {
      diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, tok,
                     "%s",
                     diag_message_for(DIAG_ERR_PARSER_RETURN_VALUE_REQUIRED_NONVOID));
    }
    return;
  }

  if (ret.is_void) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, tok,
                   "%s",
                   diag_message_for(DIAG_ERR_PARSER_RETURN_VALUE_FORBIDDEN_VOID));
  }

  /* C11 6.8.6.4 / 6.5.16.1: NULL pointer constant 0 is allowed, but a nonzero
   * integer constant cannot be returned from a pointer-returning function. */
  if (ret.is_pointer && node->lhs->kind == ND_NUM) {
    node_num_t *num = (node_num_t *)node->lhs;
    if (num->val != 0) {
      ps_diag_ctx((token_t *)tok, "return",
                   "ポインタを返す関数から非ゼロ整数定数 (%lld) を返却できません (C11 6.8.6.4)",
                   num->val);
    }
  }

}

static void semantic_transform_node_array(node_t **nodes, node_func_t *current_func,
                                          const token_t *fallback_diag_tok) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++) {
    semantic_transform_node(nodes[i], current_func, fallback_diag_tok);
  }
}

static void semantic_validate_assignment(node_t *node,
                                         const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_ASSIGN || !node->lhs || !node->rhs) return;
  token_t *tok = node->tok ? node->tok : (token_t *)fallback_diag_tok;

  psx_type_t *rhs_type = ps_node_get_type(node->rhs);
  if (rhs_type && rhs_type->kind == PSX_TYPE_VOID) {
    if (node->rhs->kind == ND_FUNCALL) {
      node_func_t *fn = (node_func_t *)node->rhs;
      if (!fn->callee && fn->funcname) {
        ps_diag_ctx(tok, "assign",
                     "void 戻り値関数の結果は代入/初期化に使えません: '%.*s' (C11 6.5.16)",
                     fn->funcname_len, fn->funcname);
      }
    }
    ps_diag_ctx(tok, "assign",
                 "void 戻り値関数の結果は代入/初期化に使えません (C11 6.5.16)");
  }

  if (node->is_decl_initializer) {
    psx_type_t *lhs_type = ps_node_get_type(node->lhs);
    int lhs_is_pointer = lhs_type && ps_type_is_pointer(lhs_type);
    ps_node_reject_const_qual_discard_at(node->lhs, node->rhs, tok);
    if (lhs_is_pointer && node->rhs->kind == ND_NUM &&
        ((node_num_t *)node->rhs)->val != 0) {
      ps_diag_ctx(tok, "init",
                   "ポインタ変数を非ゼロ整数定数 (%lld) で初期化できません (C11 6.5.16.1)",
                   ((node_num_t *)node->rhs)->val);
    }
    if (!lhs_is_pointer && lhs_type &&
        !ps_type_is_tag_aggregate(lhs_type) &&
        lhs_type->kind != PSX_TYPE_ARRAY) {
      if (ps_node_is_pointer(node->rhs)) {
        ps_diag_ctx(tok, "init",
                     "スカラ変数をポインタ型で初期化できません (C11 6.5.16.1)");
      }
      if (ps_node_aggregate_value_size(node->rhs) > 0) {
        token_kind_t rhs_tag_kind = TK_EOF;
        ps_node_get_tag_type(node->rhs, &rhs_tag_kind, NULL, NULL, NULL);
        ps_diag_ctx(tok, "init",
                     "スカラ変数を %s 値で初期化できません (C11 6.5.16.1)",
                     ps_ctx_tag_kind_spelling(rhs_tag_kind));
      }
    }
  }

  if (!node->is_source_assignment &&
      !node->is_source_compound_assignment) return;
  if (node->lhs->kind == ND_FUNCREF) {
    ps_diag_ctx(tok, "assign",
                 "関数識別子に代入することはできません (C11 6.5.16p2)");
  }
  ps_node_expect_lvalue_at(node->lhs, "=", tok);
  ps_node_reject_const_assign_at(node->lhs, "=", tok);
  if (node->is_source_assignment)
    ps_node_reject_const_qual_discard_at(node->lhs, node->rhs, tok);
}

static void semantic_transform_node(node_t *node, node_func_t *current_func,
                                    const token_t *fallback_diag_tok) {
  if (!node) return;

  switch (node->kind) {
    case ND_DECL_INIT: {
      node_decl_init_t *init = (node_decl_init_t *)node;
      semantic_transform_node(node->lhs, current_func, fallback_diag_tok);
      if (init->init_kind == PSX_DECL_INIT_LIST) {
        semantic_transform_initializer_syntax(
            node->rhs, current_func, fallback_diag_tok);
      } else {
        semantic_transform_node(node->rhs, current_func, fallback_diag_tok);
      }
      semantic_validate_assignment(node, fallback_diag_tok);
      break;
    }
    case ND_RETURN:
      semantic_transform_return(node, current_func, fallback_diag_tok);
      semantic_transform_node(node->lhs, current_func, fallback_diag_tok);
      break;
    case ND_BLOCK:
      semantic_transform_node_array(((node_block_t *)node)->body, current_func, fallback_diag_tok);
      break;
    case ND_FUNCDEF: {
      node_func_t *fn = (node_func_t *)node;
      semantic_transform_node_array(fn->args, fn, fallback_diag_tok);
      semantic_transform_node(node->rhs, fn, fallback_diag_tok);
      break;
    }
    case ND_FUNCALL: {
      node_func_t *fn = (node_func_t *)node;
      semantic_transform_node(fn->callee, current_func, fallback_diag_tok);
      for (int i = 0; i < fn->nargs; i++) {
        semantic_transform_node(fn->args[i], current_func, fallback_diag_tok);
      }
      break;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *ctrl = (node_ctrl_t *)node;
      semantic_transform_node(ctrl->init, current_func, fallback_diag_tok);
      semantic_transform_node(node->lhs, current_func, fallback_diag_tok);
      semantic_transform_node(node->rhs, current_func, fallback_diag_tok);
      semantic_transform_node(ctrl->inc, current_func, fallback_diag_tok);
      semantic_transform_node(ctrl->els, current_func, fallback_diag_tok);
      break;
    }
    default:
      semantic_transform_node(node->lhs, current_func, fallback_diag_tok);
      semantic_transform_node(node->rhs, current_func, fallback_diag_tok);
      semantic_validate_assignment(node, fallback_diag_tok);
      break;
  }
}

static int semantic_fp_literal_fractional_part_known(double f) {
  /* Diagnostic-only cast.  In the selfhost wasm compiler, out-of-i32-range
   * f64->int casts can trap, so keep the check inside that safe range. */
  if (f < -2147483648.0 || f > 2147483647.0) return 0;
  return f != (double)(long long)f;
}

static tk_float_kind_t semantic_node_fp_kind(node_t *node) {
  if (!node) return TK_FLOAT_KIND_NONE;
  psx_type_t *type = ps_node_get_type(node);
  if (type && !ps_type_is_pointer(type) &&
      (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX)) {
    return type->fp_kind != TK_FLOAT_KIND_NONE ? type->fp_kind : TK_FLOAT_KIND_DOUBLE;
  }
  return TK_FLOAT_KIND_NONE;
}

static void semantic_warn_float_to_int_expr(node_t *value, const token_t *tok,
                                            const char *literal_fmt,
                                            const char *value_msg) {
  if (!value || semantic_node_fp_kind(value) == TK_FLOAT_KIND_NONE) return;
  if (value->kind == ND_NUM) {
    double f = ((node_num_t *)value)->fval;
    if (semantic_fp_literal_fractional_part_known(f)) {
      diag_warn_tokf(DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING, tok, literal_fmt, f);
    }
  } else {
    diag_warn_tokf(DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING, tok, "%s", value_msg);
  }
}

static void semantic_warn_decl_initializer_constant_overflow(node_t *lhs, node_t *rhs,
                                                            const token_t *tok) {
  if (!lhs || !rhs || lhs->kind != ND_LVAR || rhs->kind != ND_NUM) return;
  if (semantic_node_fp_kind(lhs) != TK_FLOAT_KIND_NONE ||
      semantic_node_fp_kind(rhs) != TK_FLOAT_KIND_NONE) {
    return;
  }
  if (ps_node_is_pointer(lhs)) return;
  if (ps_node_aggregate_value_size(lhs) > 0) return;
  psx_type_t *lhs_type = ps_node_get_type(lhs);
  if (lhs_type && lhs_type->kind == PSX_TYPE_BOOL) return;
  int type_size = ps_node_type_size(lhs);
  if (type_size <= 0 || type_size >= 4) return;

  long long v = ((node_num_t *)rhs)->val;
  int bits = type_size * 8;
  long long max_signed = (1LL << (bits - 1)) - 1;
  long long min_signed = -(1LL << (bits - 1));
  long long max_unsigned = (1LL << bits) - 1;
  int out_of_range;
  if (ps_node_integer_value_is_unsigned(lhs)) {
    out_of_range = (v < 0 || v > max_unsigned);
    if (v < 0 && v >= min_signed) out_of_range = 0;
  } else {
    out_of_range = (v < min_signed || v > max_signed);
  }
  if (out_of_range) {
    diag_warn_tokf(DIAG_WARN_PARSER_CONSTANT_OVERFLOW, tok,
                   "整数リテラル %lld は %d バイト型に収まりません (値が切り詰められます)",
                   v, type_size);
  }
}

static void semantic_warn_assignment(node_t *node, const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_ASSIGN ||
      (!node->is_source_assignment && !node->is_decl_initializer)) return;
  node_t *lhs = node->lhs;
  node_t *rhs = node->rhs;
  const token_t *tok = node->tok ? node->tok : fallback_diag_tok;

  /* 自己代入 `x = x` の警告 (両辺が同じ宣言元 lvar)。
   * parser-time offset 比較では shadowing や合成 lvar に弱いため、semantic pass では
   * node_lvar_t::var を使って source lvar identity で判定する。 */
  if (node->is_source_assignment &&
      lhs && lhs->kind == ND_LVAR && rhs && rhs->kind == ND_LVAR &&
      ps_node_lvar_symbol(lhs) && ps_node_lvar_symbol(lhs) == ps_node_lvar_symbol(rhs)) {
    diag_warn_tokf(DIAG_WARN_PARSER_SELF_ASSIGN, tok,
                   "変数を自身に代入しています (タイプミスの可能性)");
  }

  /* 浮動小数点 -> 整数の縮小変換警告。source assignment だけを対象にし、
   * 宣言初期化や lowering 用の合成 assignment は既存の専用経路に任せる。 */
  if (lhs && rhs && !ps_node_is_pointer(lhs) &&
      semantic_node_fp_kind(lhs) == TK_FLOAT_KIND_NONE &&
      semantic_node_fp_kind(rhs) != TK_FLOAT_KIND_NONE) {
    if (node->is_decl_initializer) {
      semantic_warn_float_to_int_expr(
          rhs, tok,
          "整数変数を浮動小数点リテラル %g で初期化しています (小数部が切り捨てられます)",
          "整数変数を浮動小数点値で初期化しています (小数部が切り捨てられます)");
    } else {
      semantic_warn_float_to_int_expr(
          rhs, tok,
          "整数変数に浮動小数点リテラル %g を代入しています (小数部が切り捨てられます)",
          "整数変数に浮動小数点値を代入しています (小数部が切り捨てられます)");
    }
  }

  if (node->is_decl_initializer) {
    semantic_warn_decl_initializer_constant_overflow(lhs, rhs, tok);
  }
}

static void semantic_warn_return(node_t *node, node_func_t *current_func,
                                 const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_RETURN || !node->lhs || !current_func) return;
  semantic_return_type_view_t ret = semantic_return_type_view(current_func);
  if (semantic_node_fp_kind(node->lhs) != TK_FLOAT_KIND_NONE &&
      ret.fp_kind == TK_FLOAT_KIND_NONE &&
      !ret.is_pointer &&
      !ret.is_void) {
    const token_t *tok = node->tok ? node->tok : fallback_diag_tok;
    semantic_warn_float_to_int_expr(
        node->lhs, tok,
        "整数戻り型の関数から浮動小数点リテラル %g を return しています (小数部が切り捨てられます)",
        "整数戻り型の関数から浮動小数点値を return しています (小数部が切り捨てられます)");
  }

  if (ret.is_pointer && node->lhs->kind == ND_ADDR && node->lhs->lhs &&
      node->lhs->lhs->kind == ND_LVAR) {
    lvar_t *src = ps_node_lvar_symbol(node->lhs->lhs);
    if (src && !src->is_static_local) {
      const token_t *tok = node->tok ? node->tok : fallback_diag_tok;
      diag_warn_tokf(DIAG_WARN_PARSER_RETURN_STACK_ADDRESS, tok,
                     "ローカル変数 '%.*s' のアドレスを返しています (dangling pointer になります)",
                     src->len, src->name);
    }
  }
}

static const char *semantic_source_op_text(token_kind_t op) {
  switch (op) {
    case TK_OROR: return "||";
    case TK_ANDAND: return "&&";
    case TK_EQEQ: return "==";
    case TK_NEQ: return "!=";
    case TK_LT: return "<";
    case TK_LE: return "<=";
    case TK_GT: return ">";
    case TK_GE: return ">=";
    case TK_PLUS: return "+";
    case TK_MINUS: return "-";
    case TK_MUL: return "*";
    case TK_DIV: return "/";
    case TK_MOD: return "%";
    case TK_SHL: return "<<";
    case TK_SHR: return ">>";
    default: return "";
  }
}

static void semantic_source_compare_operands(node_t *node, node_t **out_lhs, node_t **out_rhs) {
  if (!node) {
    if (out_lhs) *out_lhs = NULL;
    if (out_rhs) *out_rhs = NULL;
    return;
  }
  if (node->source_op == TK_GT || node->source_op == TK_GE) {
    if (out_lhs) *out_lhs = node->rhs;
    if (out_rhs) *out_rhs = node->lhs;
  } else {
    if (out_lhs) *out_lhs = node->lhs;
    if (out_rhs) *out_rhs = node->rhs;
  }
}

static int semantic_nodes_identity_equal(node_t *lhs, node_t *rhs) {
  if (!lhs || !rhs || lhs->kind != rhs->kind) return 0;
  if (lhs->kind == ND_LVAR) {
    lvar_t *lv = ps_node_lvar_symbol(lhs);
    return lv && lv == ps_node_lvar_symbol(rhs);
  }
  if (lhs->kind == ND_GVAR) {
    node_gvar_t *lg = (node_gvar_t *)lhs;
    node_gvar_t *rg = (node_gvar_t *)rhs;
    return lg->name_len == rg->name_len &&
           memcmp(lg->name, rg->name, (size_t)lg->name_len) == 0;
  }
  if (lhs->kind == ND_NUM) {
    return ((node_num_t *)lhs)->val == ((node_num_t *)rhs)->val &&
           semantic_node_fp_kind(lhs) == semantic_node_fp_kind(rhs);
  }
  return 0;
}

static void semantic_warn_identical_logical(node_t *node, const token_t *fallback_diag_tok) {
  if (!node || (node->source_op != TK_OROR && node->source_op != TK_ANDAND)) return;
  if (!semantic_nodes_identity_equal(node->lhs, node->rhs)) return;
  const char *op = semantic_source_op_text(node->source_op);
  diag_warn_tokf(DIAG_WARN_PARSER_IDENTICAL_LOGICAL_OPERANDS,
                 node->tok ? node->tok : fallback_diag_tok,
                 "'%s' の両辺が同じ式です (常に同じ結果、タイプミスの可能性)", op);
}

static int semantic_sign_cmp_effective_unsigned(node_t *n) {
  return ps_node_integer_promotion_is_unsigned(n);
}

static void semantic_warn_sign_compare(node_t *lhs, node_t *rhs, const char *op,
                                       const token_t *tok) {
  if (!lhs || !rhs) return;
  int lu = semantic_sign_cmp_effective_unsigned(lhs);
  int ru = semantic_sign_cmp_effective_unsigned(rhs);
  if (lu == ru) return;
  node_t *signed_side = lu ? rhs : lhs;
  if (signed_side->kind == ND_NUM && ((node_num_t *)signed_side)->val >= 0) return;
  if (!ps_node_usual_arith_operands_is_unsigned(lhs, rhs)) return;
  diag_warn_tokf(DIAG_WARN_PARSER_SIGN_COMPARE, tok,
                 "符号付きと符号なしの整数を比較しています ('%s' / 負値が大きな正の値として扱われる可能性)",
                 op);
}

static int semantic_tuz_is_zero_literal(node_t *n) {
  return n && n->kind == ND_NUM && semantic_node_fp_kind(n) == TK_FLOAT_KIND_NONE &&
         ((node_num_t *)n)->val == 0;
}

static int semantic_tuz_is_unsigned_integer(node_t *n) {
  return ps_node_integer_value_is_unsigned(n);
}

static void semantic_warn_tautological_unsigned_zero(node_t *lhs, node_t *rhs, const char *op,
                                                     const token_t *tok) {
  if (semantic_tuz_is_unsigned_integer(lhs) && semantic_tuz_is_zero_literal(rhs)) {
    if (op[0] == '>' && op[1] == '=') {
      diag_warn_tokf(DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, tok,
                     "符号なし整数は常に 0 以上です: '%s 0' は常に真", op);
    } else if (op[0] == '<' && op[1] == '\0') {
      diag_warn_tokf(DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, tok,
                     "符号なし整数は常に 0 以上です: '%s 0' は常に偽", op);
    }
  }
  if (semantic_tuz_is_zero_literal(lhs) && semantic_tuz_is_unsigned_integer(rhs)) {
    if (op[0] == '<' && op[1] == '=') {
      diag_warn_tokf(DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, tok,
                     "符号なし整数は常に 0 以上です: '0 %s' は常に真", op);
    } else if (op[0] == '>' && op[1] == '\0') {
      diag_warn_tokf(DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, tok,
                     "符号なし整数は常に 0 以上です: '0 %s' は常に偽", op);
    }
  }
}

static void semantic_warn_self_compare(node_t *lhs, node_t *rhs, const char *op,
                                       const token_t *tok) {
  if (semantic_nodes_identity_equal(lhs, rhs)) {
    diag_warn_tokf(DIAG_WARN_PARSER_SELF_COMPARE, tok,
                   "自己比較 (常に同じ値): '%s'", op);
  }
}

static void semantic_warn_pointer_int_compare(node_t *lhs, node_t *rhs, const char *op,
                                              const token_t *tok) {
  if (!lhs || !rhs) return;
  node_t *p = NULL, *n = NULL;
  if (ps_node_is_pointer(lhs) && !ps_node_is_pointer(rhs) && rhs->kind == ND_NUM) {
    p = lhs; n = rhs;
  } else if (ps_node_is_pointer(rhs) && !ps_node_is_pointer(lhs) && lhs->kind == ND_NUM) {
    p = rhs; n = lhs;
  }
  if (!p) return;
  node_num_t *num = (node_num_t *)n;
  if (num->val == 0) return;
  (void)p;
  diag_warn_tokf(DIAG_WARN_PARSER_POINTER_INTEGER_COMPARE, tok,
                 "ポインタを非ゼロ整数定数 (%lld) と '%s' で比較しています (C11 6.5.16.1)",
                 num->val, op);
}

static void semantic_warn_logical_not_paren_trap(node_t *lhs, const char *op,
                                                 const token_t *tok) {
  if (!lhs || !lhs->from_logical_not) return;
  diag_warn_tokf(DIAG_WARN_PARSER_LOGICAL_NOT_PARENTHESES, tok,
                 "'%s' の左辺が単項 '!' で、'!' の優先順位が '%s' より高いため "
                 "'(!x) %s y' と解釈されます ('!(x %s y)' を意図していませんか)",
                 op, op, op, op);
}

static void semantic_warn_comparison(node_t *node, const token_t *fallback_diag_tok) {
  if (!node) return;
  token_kind_t op_kind = node->source_op;
  if (op_kind != TK_EQEQ && op_kind != TK_NEQ &&
      op_kind != TK_LT && op_kind != TK_LE &&
      op_kind != TK_GT && op_kind != TK_GE) {
    return;
  }
  node_t *lhs = NULL;
  node_t *rhs = NULL;
  semantic_source_compare_operands(node, &lhs, &rhs);
  const char *op = semantic_source_op_text(op_kind);
  const token_t *tok = node->tok ? node->tok : fallback_diag_tok;

  if (op_kind == TK_EQEQ || op_kind == TK_NEQ) {
    semantic_warn_logical_not_paren_trap(lhs, op, tok);
    semantic_warn_self_compare(lhs, rhs, op, tok);
    semantic_warn_sign_compare(lhs, rhs, op, tok);
    semantic_warn_pointer_int_compare(lhs, rhs, op, tok);
  } else {
    semantic_warn_sign_compare(lhs, rhs, op, tok);
    semantic_warn_tautological_unsigned_zero(lhs, rhs, op, tok);
  }
}

/* Integer literal overflow warnings are attached to source arithmetic nodes
 * only.  Semantic lowering also creates ND_ADD/ND_MUL/ND_DIV nodes for pointer
 * scaling and pointer differences; those synthetic nodes intentionally keep
 * source_op == TK_EOF and are ignored here. */
static int semantic_int_const_overflow_is_int_literal(node_t *node) {
  if (!node || node->kind != ND_NUM) return 0;
  if (semantic_node_fp_kind(node) != TK_FLOAT_KIND_NONE) return 0;
  if (ps_node_integer_value_is_unsigned(node)) return 0;
  node_num_t *num = (node_num_t *)node;
  if (num->int_is_long || num->int_is_long_long) return 0;
  return num->val >= -2147483648LL && num->val <= 2147483647LL;
}

static void semantic_warn_int_const_overflow(node_t *node, const token_t *tok) {
  if (!node || (node->source_op != TK_PLUS && node->source_op != TK_MINUS &&
                node->source_op != TK_MUL)) return;
  if (!semantic_int_const_overflow_is_int_literal(node->lhs) ||
      !semantic_int_const_overflow_is_int_literal(node->rhs)) return;

  long long a = ((node_num_t *)node->lhs)->val;
  long long b = ((node_num_t *)node->rhs)->val;
  long long r;
  if (node->source_op == TK_PLUS) r = a + b;
  else if (node->source_op == TK_MINUS) r = a - b;
  else r = a * b;

  if (r < -2147483648LL || r > 2147483647LL) {
    diag_warn_tokf(DIAG_WARN_PARSER_INTEGER_OVERFLOW, tok,
                   "整数定数式 %lld %s %lld = %lld は int の範囲を超えています (C11 6.5p5 未定義動作)",
                   a, semantic_source_op_text(node->source_op), b, r);
  }
}

static void semantic_warn_shift_out_of_range(node_t *node, const token_t *tok) {
  if (!node || (node->source_op != TK_SHL && node->source_op != TK_SHR)) return;
  node_t *rhs = node->rhs;
  if (!rhs || rhs->kind != ND_NUM) return;
  long long r = ((node_num_t *)rhs)->val;
  int lhs_ts = node->lhs ? ps_node_type_size(node->lhs) : 4;
  int width = (lhs_ts >= 8) ? 64 : 32;
  if (r < 0 || r >= width) {
    diag_warn_tokf(DIAG_WARN_PARSER_SHIFT_OUT_OF_RANGE, tok,
                   "シフト量 %lld が型の幅 (%d ビット) を超えています (C11 6.5.7p3 未定義動作): %s",
                   r, width, semantic_source_op_text(node->source_op));
  }
}

static void semantic_warn_divide_by_zero(node_t *node, const token_t *tok) {
  if (!node || (node->source_op != TK_DIV && node->source_op != TK_MOD)) return;
  node_t *rhs = node->rhs;
  if (!rhs || rhs->kind != ND_NUM ||
      semantic_node_fp_kind(rhs) != TK_FLOAT_KIND_NONE) {
    return;
  }
  if (((node_num_t *)rhs)->val != 0) return;
  if (node->source_op == TK_DIV) {
    diag_warn_tokf(DIAG_WARN_PARSER_DIVIDE_BY_ZERO, tok,
                   "0 による除算 (C11 6.5.5p5 未定義動作)");
  } else {
    diag_warn_tokf(DIAG_WARN_PARSER_DIVIDE_BY_ZERO, tok,
                   "0 による剰余 (C11 6.5.5p5 未定義動作)");
  }
}

static void semantic_warn_arithmetic(node_t *node, const token_t *fallback_diag_tok) {
  if (!node || node->source_op == TK_EOF) return;
  const token_t *tok = node->tok ? node->tok : fallback_diag_tok;
  semantic_warn_int_const_overflow(node, tok);
  semantic_warn_shift_out_of_range(node, tok);
  semantic_warn_divide_by_zero(node, tok);
}

static void semantic_warn_funcall(node_t *node, const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_FUNCALL || !node->is_implicit_func_decl) return;
  node_func_t *fn = (node_func_t *)node;
  if (!fn->funcname) return;
  diag_warn_tokf(DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL,
                 node->tok ? node->tok : fallback_diag_tok,
                 "関数 '%.*s' は宣言されていません (C99/C11 で implicit declaration は不可)",
                 fn->funcname_len, fn->funcname);
}

static void semantic_warn_funcdef(node_t *node, const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_FUNCDEF || !node->is_implicit_int_return) return;
  diag_warn_tokf(DIAG_WARN_PARSER_IMPLICIT_INT_RETURN,
                 node->tok ? node->tok : fallback_diag_tok,
                 "%s", diag_warn_message_for(DIAG_WARN_PARSER_IMPLICIT_INT_RETURN));
}

static const char *semantic_condition_context(node_kind_t kind) {
  switch (kind) {
    case ND_IF: return "if 文";
    case ND_WHILE: return "while 文";
    default: return NULL;
  }
}

static void semantic_warn_condition(node_t *node, const token_t *fallback_diag_tok) {
  if (!node) return;
  const char *ctx = semantic_condition_context(node->kind);
  if (!ctx) return;
  node_t *cond = node->lhs;
  const token_t *tok = node->tok ? node->tok : fallback_diag_tok;
  if (cond && cond->kind == ND_ASSIGN) {
    diag_warn_tokf(DIAG_WARN_PARSER_ASSIGN_IN_CONDITION, tok,
                   "%s の条件に代入式を使っています ('==' のタイプミスの可能性)",
                   ctx);
  } else if (cond && cond->kind == ND_COMMA) {
    diag_warn_tokf(DIAG_WARN_PARSER_COMMA_IN_CONDITION, tok,
                   "%s の条件にカンマ演算子を使っています ('&&' のタイプミスの可能性)",
                   ctx);
  }
  if (node->kind == ND_IF && node->has_empty_body) {
    diag_warn_tokf(DIAG_WARN_PARSER_EMPTY_BODY, tok,
                   "if 文の本体が空です (タイプミスの可能性)");
  }
}

static void semantic_warn_node_array(node_t **nodes, node_func_t *current_func,
                                     const token_t *fallback_diag_tok) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++) semantic_warn_node(nodes[i], current_func, fallback_diag_tok);
}

static void semantic_warn_node(node_t *node, node_func_t *current_func,
                               const token_t *fallback_diag_tok) {
  if (!node) return;

  switch (node->kind) {
    case ND_ASSIGN:
      semantic_warn_assignment(node, fallback_diag_tok);
      semantic_warn_node(node->lhs, current_func, fallback_diag_tok);
      semantic_warn_node(node->rhs, current_func, fallback_diag_tok);
      break;
    case ND_RETURN:
      semantic_warn_return(node, current_func, fallback_diag_tok);
      semantic_warn_node(node->lhs, current_func, fallback_diag_tok);
      break;
    case ND_LOGOR:
    case ND_LOGAND:
      semantic_warn_identical_logical(node, fallback_diag_tok);
      semantic_warn_node(node->lhs, current_func, fallback_diag_tok);
      semantic_warn_node(node->rhs, current_func, fallback_diag_tok);
      break;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
      semantic_warn_comparison(node, fallback_diag_tok);
      semantic_warn_node(node->lhs, current_func, fallback_diag_tok);
      semantic_warn_node(node->rhs, current_func, fallback_diag_tok);
      break;
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_SHL:
    case ND_SHR:
      semantic_warn_arithmetic(node, fallback_diag_tok);
      semantic_warn_node(node->lhs, current_func, fallback_diag_tok);
      semantic_warn_node(node->rhs, current_func, fallback_diag_tok);
      break;
    case ND_BLOCK: {
      semantic_warn_node_array(((node_block_t *)node)->body, current_func, fallback_diag_tok);
      break;
    }
    case ND_FUNCDEF: {
      node_func_t *fn = (node_func_t *)node;
      semantic_warn_funcdef(node, fallback_diag_tok);
      semantic_warn_node_array(fn->args, fn, fallback_diag_tok);
      semantic_warn_node(node->rhs, fn, fallback_diag_tok);
      break;
    }
    case ND_FUNCALL: {
      node_func_t *fn = (node_func_t *)node;
      semantic_warn_funcall(node, fallback_diag_tok);
      semantic_warn_node(fn->callee, current_func, fallback_diag_tok);
      for (int i = 0; i < fn->nargs; i++) semantic_warn_node(fn->args[i], current_func, fallback_diag_tok);
      break;
    }
    case ND_IF:
    case ND_WHILE:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *ctrl = (node_ctrl_t *)node;
      semantic_warn_condition(node, fallback_diag_tok);
      semantic_warn_node(ctrl->init, current_func, fallback_diag_tok);
      semantic_warn_node(node->lhs, current_func, fallback_diag_tok);
      semantic_warn_node(node->rhs, current_func, fallback_diag_tok);
      semantic_warn_node(ctrl->inc, current_func, fallback_diag_tok);
      semantic_warn_node(ctrl->els, current_func, fallback_diag_tok);
      break;
    }
    default:
      semantic_warn_node(node->lhs, current_func, fallback_diag_tok);
      semantic_warn_node(node->rhs, current_func, fallback_diag_tok);
      break;
  }
}

static void semantic_validate_control_flow_array(node_t **nodes, const token_t *fallback_diag_tok,
                                                 int loop_depth, int switch_depth) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++) {
    semantic_validate_control_flow(nodes[i], fallback_diag_tok, loop_depth, switch_depth);
  }
}

typedef struct {
  long long *case_vals;
  int ncase;
  int cap;
  int has_default;
} semantic_switch_label_ctx_t;

static void semantic_switch_label_ctx_free(semantic_switch_label_ctx_t *ctx) {
  if (!ctx) return;
  free(ctx->case_vals);
  ctx->case_vals = NULL;
  ctx->ncase = 0;
  ctx->cap = 0;
  ctx->has_default = 0;
}

static void semantic_switch_register_case(semantic_switch_label_ctx_t *ctx,
                                          node_case_t *case_node,
                                          const token_t *fallback_diag_tok) {
  const token_t *tok = case_node->base.tok ? case_node->base.tok : fallback_diag_tok;
  for (int i = 0; i < ctx->ncase; i++) {
    if (ctx->case_vals[i] == case_node->val) {
      diag_emit_tokf(DIAG_ERR_PARSER_SWITCH_DUPLICATE_CASE, tok,
                     diag_message_for(DIAG_ERR_PARSER_SWITCH_DUPLICATE_CASE),
                     case_node->val);
    }
  }
  if (ctx->ncase >= ctx->cap) {
    ctx->cap = pda_next_cap(ctx->cap, ctx->ncase + 1);
    ctx->case_vals = pda_xreallocarray(ctx->case_vals, (size_t)ctx->cap,
                                       sizeof(long long));
  }
  ctx->case_vals[ctx->ncase++] = case_node->val;
}

static void semantic_switch_register_default(semantic_switch_label_ctx_t *ctx,
                                             node_t *default_node,
                                             const token_t *fallback_diag_tok) {
  const token_t *tok = default_node->tok ? default_node->tok : fallback_diag_tok;
  if (ctx->has_default) {
    diag_emit_tokf(DIAG_ERR_PARSER_SWITCH_DUPLICATE_DEFAULT, tok, "%s",
                   diag_message_for(DIAG_ERR_PARSER_SWITCH_DUPLICATE_DEFAULT));
  }
  ctx->has_default = 1;
}

static void semantic_collect_switch_labels_in_current_switch(node_t *node,
                                                            semantic_switch_label_ctx_t *ctx,
                                                            const token_t *fallback_diag_tok);

static void semantic_collect_switch_labels_array(node_t **nodes,
                                                 semantic_switch_label_ctx_t *ctx,
                                                 const token_t *fallback_diag_tok) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++) {
    semantic_collect_switch_labels_in_current_switch(nodes[i], ctx, fallback_diag_tok);
  }
}

static void semantic_collect_switch_labels_in_current_switch(node_t *node,
                                                            semantic_switch_label_ctx_t *ctx,
                                                            const token_t *fallback_diag_tok) {
  if (!node) return;

  switch (node->kind) {
    case ND_SWITCH:
      /* Nested switch owns its own case/default namespace and will be validated
       * when the main semantic walk reaches that ND_SWITCH. */
      return;
    case ND_CASE:
      semantic_switch_register_case(ctx, (node_case_t *)node, fallback_diag_tok);
      semantic_collect_switch_labels_in_current_switch(node->rhs, ctx, fallback_diag_tok);
      return;
    case ND_DEFAULT:
      semantic_switch_register_default(ctx, node, fallback_diag_tok);
      semantic_collect_switch_labels_in_current_switch(node->rhs, ctx, fallback_diag_tok);
      return;
    case ND_BLOCK:
      semantic_collect_switch_labels_array(((node_block_t *)node)->body, ctx, fallback_diag_tok);
      return;
    case ND_FUNCALL: {
      node_func_t *fn = (node_func_t *)node;
      semantic_collect_switch_labels_in_current_switch(fn->callee, ctx, fallback_diag_tok);
      semantic_collect_switch_labels_array(fn->args, ctx, fallback_diag_tok);
      return;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *ctrl = (node_ctrl_t *)node;
      semantic_collect_switch_labels_in_current_switch(ctrl->init, ctx, fallback_diag_tok);
      semantic_collect_switch_labels_in_current_switch(node->lhs, ctx, fallback_diag_tok);
      semantic_collect_switch_labels_in_current_switch(node->rhs, ctx, fallback_diag_tok);
      semantic_collect_switch_labels_in_current_switch(ctrl->inc, ctx, fallback_diag_tok);
      semantic_collect_switch_labels_in_current_switch(ctrl->els, ctx, fallback_diag_tok);
      return;
    }
    default:
      semantic_collect_switch_labels_in_current_switch(node->lhs, ctx, fallback_diag_tok);
      semantic_collect_switch_labels_in_current_switch(node->rhs, ctx, fallback_diag_tok);
      return;
  }
}

static void semantic_validate_switch_labels(node_t *node, const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_SWITCH) return;
  semantic_switch_label_ctx_t ctx = {0};
  semantic_collect_switch_labels_in_current_switch(node->rhs, &ctx, fallback_diag_tok);
  semantic_switch_label_ctx_free(&ctx);
}

static void semantic_validate_control_flow(node_t *node, const token_t *fallback_diag_tok,
                                           int loop_depth, int switch_depth) {
  if (!node) return;
  const token_t *tok = node->tok ? node->tok : fallback_diag_tok;

  switch (node->kind) {
    case ND_BREAK:
      if (loop_depth == 0 && switch_depth == 0) {
        ps_diag_only_in((token_t *)tok, diag_text_for(DIAG_TEXT_BREAK),
                         diag_text_for(DIAG_TEXT_LOOP_OR_SWITCH_SCOPE));
      }
      return;
    case ND_CONTINUE:
      if (loop_depth == 0) {
        ps_diag_only_in((token_t *)tok, diag_text_for(DIAG_TEXT_CONTINUE),
                         diag_text_for(DIAG_TEXT_LOOP_SCOPE));
      }
      return;
    case ND_BLOCK:
      semantic_validate_control_flow_array(((node_block_t *)node)->body, fallback_diag_tok,
                                           loop_depth, switch_depth);
      return;
    case ND_FUNCDEF: {
      node_func_t *fn = (node_func_t *)node;
      semantic_validate_control_flow_array(fn->args, fallback_diag_tok, loop_depth, switch_depth);
      semantic_validate_control_flow(node->rhs, fallback_diag_tok, loop_depth, switch_depth);
      return;
    }
    case ND_FUNCALL: {
      node_func_t *fn = (node_func_t *)node;
      semantic_validate_control_flow(fn->callee, fallback_diag_tok, loop_depth, switch_depth);
      for (int i = 0; i < fn->nargs; i++) {
        semantic_validate_control_flow(fn->args[i], fallback_diag_tok, loop_depth, switch_depth);
      }
      return;
    }
    case ND_WHILE:
      semantic_validate_control_flow(node->lhs, fallback_diag_tok, loop_depth, switch_depth);
      semantic_validate_control_flow(node->rhs, fallback_diag_tok, loop_depth + 1, switch_depth);
      return;
    case ND_DO_WHILE:
      semantic_validate_control_flow(node->rhs, fallback_diag_tok, loop_depth + 1, switch_depth);
      semantic_validate_control_flow(node->lhs, fallback_diag_tok, loop_depth, switch_depth);
      return;
    case ND_FOR: {
      node_ctrl_t *ctrl = (node_ctrl_t *)node;
      semantic_validate_control_flow(ctrl->init, fallback_diag_tok, loop_depth, switch_depth);
      semantic_validate_control_flow(node->lhs, fallback_diag_tok, loop_depth, switch_depth);
      semantic_validate_control_flow(ctrl->inc, fallback_diag_tok, loop_depth, switch_depth);
      semantic_validate_control_flow(node->rhs, fallback_diag_tok, loop_depth + 1, switch_depth);
      return;
    }
    case ND_SWITCH:
      semantic_validate_control_flow(node->lhs, fallback_diag_tok, loop_depth, switch_depth);
      semantic_validate_switch_labels(node, fallback_diag_tok);
      semantic_validate_control_flow(node->rhs, fallback_diag_tok, loop_depth, switch_depth + 1);
      return;
    case ND_CASE:
      if (switch_depth == 0) {
        ps_diag_only_in((token_t *)tok, diag_text_for(DIAG_TEXT_CASE),
                         diag_text_for(DIAG_TEXT_SWITCH_SCOPE));
      }
      semantic_validate_control_flow(node->rhs, fallback_diag_tok, loop_depth, switch_depth);
      return;
    case ND_DEFAULT:
      if (switch_depth == 0) {
        ps_diag_only_in((token_t *)tok, diag_text_for(DIAG_TEXT_DEFAULT),
                         diag_text_for(DIAG_TEXT_SWITCH_SCOPE));
      }
      semantic_validate_control_flow(node->rhs, fallback_diag_tok, loop_depth, switch_depth);
      return;
    case ND_LABEL:
      semantic_validate_control_flow(node->rhs, fallback_diag_tok, loop_depth, switch_depth);
      return;
    case ND_IF:
    case ND_TERNARY: {
      node_ctrl_t *ctrl = (node_ctrl_t *)node;
      semantic_validate_control_flow(node->lhs, fallback_diag_tok, loop_depth, switch_depth);
      semantic_validate_control_flow(node->rhs, fallback_diag_tok, loop_depth, switch_depth);
      semantic_validate_control_flow(ctrl->els, fallback_diag_tok, loop_depth, switch_depth);
      return;
    }
    case ND_STMT_EXPR:
      semantic_validate_control_flow(node->lhs, fallback_diag_tok, loop_depth, switch_depth);
      return;
    default:
      semantic_validate_control_flow(node->lhs, fallback_diag_tok, loop_depth, switch_depth);
      semantic_validate_control_flow(node->rhs, fallback_diag_tok, loop_depth, switch_depth);
      return;
  }
}

static int semantic_stmt_tail_terminates(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_RETURN:
    case ND_BREAK:
    case ND_CONTINUE:
    case ND_GOTO:
      return 1;
    case ND_CASE:
    case ND_DEFAULT:
      return semantic_stmt_tail_terminates(node->rhs);
    case ND_BLOCK: {
      node_block_t *block = (node_block_t *)node;
      if (!block->body) return 0;
      node_t *last = NULL;
      for (int i = 0; block->body[i]; i++) last = block->body[i];
      return semantic_stmt_tail_terminates(last);
    }
    default:
      return 0;
  }
}

static int semantic_stmt_direct_terminates(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_RETURN:
    case ND_BREAK:
    case ND_CONTINUE:
    case ND_GOTO:
      return 1;
    case ND_CASE:
    case ND_DEFAULT:
      return semantic_stmt_tail_terminates(node);
    default:
      return 0;
  }
}

static int semantic_stmt_resumes_reachable(node_t *node) {
  if (!node) return 0;
  return node->kind == ND_CASE || node->kind == ND_DEFAULT || node->kind == ND_LABEL;
}

static int semantic_stmt_is_switch_label(node_t *node) {
  if (!node) return 0;
  return node->kind == ND_CASE || node->kind == ND_DEFAULT;
}

static void semantic_suppress_lvar_regions_in_node(node_t *node) {
  if (!node) return;
  ps_decl_suppress_lvar_usage_region(node->usage_region);

  switch (node->kind) {
    case ND_BLOCK: {
      node_block_t *block = (node_block_t *)node;
      if (block->body) {
        for (int i = 0; block->body[i]; i++) {
          semantic_suppress_lvar_regions_in_node(block->body[i]);
        }
      }
      break;
    }
    case ND_FUNCDEF: {
      node_func_t *fn = (node_func_t *)node;
      for (int i = 0; i < fn->nargs; i++) semantic_suppress_lvar_regions_in_node(fn->args[i]);
      semantic_suppress_lvar_regions_in_node(node->rhs);
      break;
    }
    case ND_FUNCALL: {
      node_func_t *fn = (node_func_t *)node;
      semantic_suppress_lvar_regions_in_node(fn->callee);
      for (int i = 0; i < fn->nargs; i++) semantic_suppress_lvar_regions_in_node(fn->args[i]);
      break;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *ctrl = (node_ctrl_t *)node;
      semantic_suppress_lvar_regions_in_node(ctrl->init);
      semantic_suppress_lvar_regions_in_node(node->lhs);
      semantic_suppress_lvar_regions_in_node(node->rhs);
      semantic_suppress_lvar_regions_in_node(ctrl->inc);
      semantic_suppress_lvar_regions_in_node(ctrl->els);
      break;
    }
    default:
      semantic_suppress_lvar_regions_in_node(node->lhs);
      semantic_suppress_lvar_regions_in_node(node->rhs);
      break;
  }
}

static void semantic_check_unreachable_in_block(node_block_t *block,
                                                const token_t *fallback_diag_tok) {
  if (!block || !block->body) return;

  int prev_terminates = 0;
  int seen_case_in_block = 0;
  int prev_fallthrough_terminates = 0;
  int in_unreachable_run = 0;
  for (int i = 0; block->body[i]; i++) {
    node_t *stmt = block->body[i];
    if (seen_case_in_block && !prev_fallthrough_terminates &&
        semantic_stmt_is_switch_label(stmt)) {
      diag_warn_tokf(DIAG_WARN_PARSER_SWITCH_FALLTHROUGH,
                     stmt->tok ? stmt->tok : fallback_diag_tok,
                     "%s", diag_warn_message_for(DIAG_WARN_PARSER_SWITCH_FALLTHROUGH));
    }
    int resumes_reachable = semantic_stmt_resumes_reachable(stmt);
    if (resumes_reachable) in_unreachable_run = 0;
    if (prev_terminates && !resumes_reachable && !in_unreachable_run) {
      diag_warn_tokf(DIAG_WARN_PARSER_UNREACHABLE_CODE,
                     stmt->tok ? stmt->tok : fallback_diag_tok,
                     "%s", diag_warn_message_for(DIAG_WARN_PARSER_UNREACHABLE_CODE));
      in_unreachable_run = 1;
    }
    if (in_unreachable_run) semantic_suppress_lvar_regions_in_node(stmt);
    semantic_check_unreachable_in_node(stmt, fallback_diag_tok);
    prev_terminates = semantic_stmt_direct_terminates(stmt);
    prev_fallthrough_terminates = prev_terminates;
    if (semantic_stmt_is_switch_label(stmt)) seen_case_in_block = 1;
  }
}

static void semantic_check_unreachable_in_node(node_t *node, const token_t *fallback_diag_tok) {
  if (!node) return;

  switch (node->kind) {
    case ND_BLOCK:
      semantic_check_unreachable_in_block((node_block_t *)node, fallback_diag_tok);
      break;
    case ND_FUNCDEF:
      semantic_check_unreachable_in_node(node->rhs, fallback_diag_tok);
      break;
    case ND_FUNCALL: {
      node_func_t *fn = (node_func_t *)node;
      semantic_check_unreachable_in_node(fn->callee, fallback_diag_tok);
      for (int i = 0; i < fn->nargs; i++) {
        semantic_check_unreachable_in_node(fn->args[i], fallback_diag_tok);
      }
      break;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *ctrl = (node_ctrl_t *)node;
      semantic_check_unreachable_in_node(ctrl->init, fallback_diag_tok);
      semantic_check_unreachable_in_node(node->lhs, fallback_diag_tok);
      semantic_check_unreachable_in_node(node->rhs, fallback_diag_tok);
      semantic_check_unreachable_in_node(ctrl->inc, fallback_diag_tok);
      semantic_check_unreachable_in_node(ctrl->els, fallback_diag_tok);
      break;
    }
    default:
      semantic_check_unreachable_in_node(node->lhs, fallback_diag_tok);
      semantic_check_unreachable_in_node(node->rhs, fallback_diag_tok);
      break;
  }
}

static int semantic_node_is_aggregate_lvar(node_t *node) {
  if (!node || node->kind != ND_LVAR) return 0;
  return ps_node_aggregate_value_size(node) > 0;
}

static node_t *semantic_assigned_aggregate_lvar_from_member_base(node_t *base);

static node_t *semantic_assigned_aggregate_lvar_from_member_addr(node_t *addr) {
  if (!addr) return NULL;
  if (addr->kind == ND_COMMA && addr->rhs) {
    return semantic_assigned_aggregate_lvar_from_member_addr(addr->rhs);
  }
  if ((addr->kind == ND_ADD || addr->kind == ND_SUB) && addr->lhs) {
    return semantic_assigned_aggregate_lvar_from_member_addr(addr->lhs);
  }
  if (addr->kind == ND_ADDR && addr->lhs) {
    return semantic_assigned_aggregate_lvar_from_member_base(addr->lhs);
  }
  return NULL;
}

static node_t *semantic_assigned_aggregate_lvar_from_member_base(node_t *base) {
  if (!base) return NULL;
  if (semantic_node_is_aggregate_lvar(base)) return base;
  if (base->kind == ND_COMMA && base->rhs) {
    return semantic_assigned_aggregate_lvar_from_member_base(base->rhs);
  }
  if (base->kind == ND_DEREF && base->lhs) {
    return semantic_assigned_aggregate_lvar_from_member_addr(base->lhs);
  }
  return NULL;
}

static node_t *semantic_assigned_lvar_from_target(node_t *target) {
  if (!target) return NULL;
  if (target->kind == ND_LVAR) return target;
  if (target->kind == ND_DEREF && target->lhs &&
      target->lhs->kind == ND_ADDR && target->lhs->lhs &&
      target->lhs->lhs->kind == ND_LVAR) {
    return target->lhs->lhs;
  }
  if (target->kind == ND_DEREF) {
    return semantic_assigned_aggregate_lvar_from_member_addr(target->lhs);
  }
  return NULL;
}

static void semantic_record_initialized_lvar(node_t *target,
                                             psx_lvar_usage_region_t *region) {
  node_t *lvar = semantic_assigned_lvar_from_target(target);
  lvar_t *var = ps_node_lvar_symbol(lvar);
  if (var) {
    ps_decl_record_lvar_usage_in_region(var, PSX_LVAR_USAGE_INITIALIZED, region);
  }
}

static void semantic_record_address_taken_from_operand(node_t *operand,
                                                       psx_lvar_usage_region_t *region) {
  if (!operand) return;
  if (operand->kind == ND_COMMA && operand->rhs) {
    semantic_record_address_taken_from_operand(operand->rhs, region);
    return;
  }
  if (operand->kind == ND_LVAR) {
    lvar_t *var = ps_node_lvar_symbol(operand);
    if (var) ps_decl_record_lvar_usage_in_region(var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
    return;
  }
  if (operand->kind == ND_ADDR && operand->lhs) {
    if (operand->lhs->kind == ND_LVAR) {
      lvar_t *var = ps_node_lvar_symbol(operand->lhs);
      if (var) ps_decl_record_lvar_usage_in_region(var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
      return;
    }
    semantic_record_address_taken_from_operand(operand->lhs, region);
    return;
  }
  if (operand->kind == ND_DEREF) {
    node_t *lvar = semantic_assigned_aggregate_lvar_from_member_addr(operand->lhs);
    lvar_t *var = ps_node_lvar_symbol(lvar);
    if (var) ps_decl_record_lvar_usage_in_region(var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
  }
}

static void semantic_collect_lvar_usage_node_array(node_t **nodes,
                                                   psx_lvar_usage_region_t *region) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++) semantic_collect_lvar_usage_events(nodes[i], region);
}

static void semantic_collect_lvar_usage_events(node_t *node,
                                               psx_lvar_usage_region_t *inherited_region) {
  if (!node) return;
  psx_lvar_usage_region_t *region = node->usage_region ? node->usage_region : inherited_region;

  if (node->records_lvar_usage && node->usage_lvar) {
    ps_decl_record_lvar_usage_in_region(
        node->usage_lvar,
        node->lvar_usage_unevaluated ? PSX_LVAR_USAGE_UNEVALUATED : PSX_LVAR_USAGE_EVALUATED,
        region);
  }

  switch (node->kind) {
    case ND_ASSIGN:
      semantic_record_initialized_lvar(node->lhs, region);
      semantic_collect_lvar_usage_events(node->lhs, region);
      semantic_collect_lvar_usage_events(node->rhs, region);
      break;
    case ND_ADDR:
      semantic_collect_lvar_usage_events(node->lhs, region);
      if (node->is_explicit_addr_expr) {
        semantic_record_address_taken_from_operand(node, region);
      }
      break;
    case ND_BLOCK: {
      node_block_t *block = (node_block_t *)node;
      semantic_collect_lvar_usage_node_array(block->body, region);
      break;
    }
    case ND_FUNCDEF: {
      node_func_t *fn = (node_func_t *)node;
      for (int i = 0; i < fn->nargs; i++) semantic_collect_lvar_usage_events(fn->args[i], region);
      semantic_collect_lvar_usage_events(node->rhs, region);
      break;
    }
    case ND_FUNCALL: {
      node_func_t *fn = (node_func_t *)node;
      semantic_collect_lvar_usage_events(fn->callee, region);
      for (int i = 0; i < fn->nargs; i++) semantic_collect_lvar_usage_events(fn->args[i], region);
      break;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *ctrl = (node_ctrl_t *)node;
      semantic_collect_lvar_usage_events(ctrl->init, region);
      semantic_collect_lvar_usage_events(node->lhs, region);
      semantic_collect_lvar_usage_events(node->rhs, region);
      semantic_collect_lvar_usage_events(ctrl->inc, region);
      semantic_collect_lvar_usage_events(ctrl->els, region);
      break;
    }
    default:
      semantic_collect_lvar_usage_events(node->lhs, region);
      semantic_collect_lvar_usage_events(node->rhs, region);
      break;
  }
}

static void semantic_warn_unused_uninitialized_locals(node_func_t *func,
                                                      const token_t *fallback_diag_tok) {
  if (!func) return;
  for (lvar_t *v = func->lvars; v; v = v->next_all) {
    if (v->suppress_unreachable_warnings) continue;
    if (!v->is_used && !v->is_unevaluated_used && !v->is_address_taken &&
        !v->is_param && v->name[0] != '_') {
      diag_warn_tokf(DIAG_WARN_PARSER_UNUSED_VARIABLE, fallback_diag_tok,
                     diag_warn_message_for(DIAG_WARN_PARSER_UNUSED_VARIABLE),
                     v->len, v->name);
    } else if (v->is_used && !v->is_initialized && !v->is_param && !v->is_array) {
      diag_warn_tokf(DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE, fallback_diag_tok,
                     diag_warn_message_for(DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE),
                     v->len, v->name);
    }
  }
}

static void semantic_record_preinitialized_locals(node_func_t *func) {
  if (!func) return;
  for (lvar_t *v = func->lvars; v; v = v->next_all) {
    if (v->is_param) {
      ps_decl_record_lvar_usage_in_region(v, PSX_LVAR_USAGE_INITIALIZED, NULL);
    } else if (v->is_static_local) {
      ps_decl_record_lvar_usage_in_region(v, PSX_LVAR_USAGE_INITIALIZED, v->decl_region);
    }
  }
}

void psx_semantic_analyze_function(node_t *func, const token_t *fallback_diag_tok) {
  if (func && func->kind == ND_FUNCDEF) {
    node_func_t *fn = (node_func_t *)func;
    semantic_validate_control_flow(func, fallback_diag_tok, 0, 0);
    semantic_transform_node(func, fn, fallback_diag_tok);
    psx_lower_semantic_tree(func, fallback_diag_tok);
    semantic_transform_node(func, fn, fallback_diag_tok);
    /* Lowering may introduce typed temporaries. Refresh the function-owned
     * list before usage analysis and before the IR builder consumes it. */
    fn->lvars = ps_decl_get_locals();
    semantic_visit_node(func);
    semantic_warn_node(func, fn, fallback_diag_tok);
    semantic_check_unreachable_in_node(func, fallback_diag_tok);
    psx_lower_implicit_conversions(func, fn, fallback_diag_tok);
    semantic_collect_lvar_usage_events(func, NULL);
    semantic_record_preinitialized_locals(fn);
    ps_decl_replay_lvar_usage_events(fn->lvars);
    semantic_warn_unused_uninitialized_locals(fn, fallback_diag_tok);
  } else {
    semantic_visit_node(func);
  }
}

void psx_semantic_analyze_expression(node_t *expr,
                                     const token_t *fallback_diag_tok) {
  semantic_transform_node(expr, NULL, fallback_diag_tok);
  psx_lower_semantic_tree(expr, fallback_diag_tok);
  semantic_transform_node(expr, NULL, fallback_diag_tok);
  semantic_visit_node(expr);
  psx_lower_implicit_conversions(expr, NULL, fallback_diag_tok);
}

void psx_semantic_analyze_initializer_syntax(
    node_t *syntax, const token_t *fallback_diag_tok) {
  semantic_transform_initializer_syntax(
      syntax, NULL, fallback_diag_tok);
  psx_lower_semantic_initializer_syntax(
      syntax, fallback_diag_tok);
  semantic_transform_initializer_syntax(
      syntax, NULL, fallback_diag_tok);
}

void psx_semantic_analyze_program(node_t **program) {
  if (program) {
    for (int i = 0; program[i]; i++) {
      if (program[i]->kind != ND_FUNCDEF) {
        semantic_transform_node(program[i], NULL, program[i]->tok);
        psx_lower_semantic_tree(program[i], program[i]->tok);
        semantic_transform_node(program[i], NULL, program[i]->tok);
      }
    }
  }
  semantic_visit_node_array(program);
  if (program) {
    for (int i = 0; program[i]; i++) {
      if (program[i]->kind != ND_FUNCDEF) {
        psx_lower_implicit_conversions(
            program[i], NULL, program[i]->tok);
      }
    }
  }
}
