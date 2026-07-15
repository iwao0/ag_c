#include "semantic_diagnostics.h"

#include "../diag/diag.h"
#include "../parser/lvar_public.h"
#include "../parser/node_utils.h"

#include <string.h>

static int fp_literal_fractional_part_known(double value) {
  if (value < -2147483648.0 || value > 2147483647.0) return 0;
  return value != (double)(long long)value;
}

static tk_float_kind_t type_fp_kind(const psx_type_t *type) {
  if (type && !ps_type_is_pointer(type) &&
      (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX)) {
    return type->fp_kind != TK_FLOAT_KIND_NONE
               ? type->fp_kind
               : TK_FLOAT_KIND_DOUBLE;
  }
  return TK_FLOAT_KIND_NONE;
}

static tk_float_kind_t node_fp_kind(node_t *node) {
  return node ? type_fp_kind(ps_node_get_type(node))
              : TK_FLOAT_KIND_NONE;
}

static void warn_float_to_int(
    ag_diagnostic_context_t *diagnostics, node_t *value,
    const token_t *tok,
    const char *literal_fmt, const char *value_msg) {
  if (!value || node_fp_kind(value) == TK_FLOAT_KIND_NONE) return;
  if (value->kind == ND_NUM) {
    double f = ((node_num_t *)value)->fval;
    if (fp_literal_fractional_part_known(f))
      diag_warn_tokf_in(diagnostics,
          DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING, tok, literal_fmt, f);
    return;
  }
  diag_warn_tokf_in(diagnostics,
      DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING, tok, "%s", value_msg);
}

static void warn_decl_initializer_overflow(
    ag_diagnostic_context_t *diagnostics, node_t *lhs, node_t *rhs,
    const token_t *tok) {
  if (!lhs || !rhs || lhs->kind != ND_LVAR || rhs->kind != ND_NUM) return;
  if (node_fp_kind(lhs) != TK_FLOAT_KIND_NONE ||
      node_fp_kind(rhs) != TK_FLOAT_KIND_NONE ||
      ps_node_value_is_pointer_like(lhs) || ps_node_aggregate_value_size(lhs) > 0)
    return;
  const psx_type_t *lhs_type = ps_node_get_type(lhs);
  if (lhs_type && lhs_type->kind == PSX_TYPE_BOOL) return;
  int type_size = ps_node_type_size(lhs);
  if (type_size <= 0 || type_size >= 4) return;

  long long value = ((node_num_t *)rhs)->val;
  int bits = type_size * 8;
  long long max_signed = (1LL << (bits - 1)) - 1;
  long long min_signed = -(1LL << (bits - 1));
  long long max_unsigned = (1LL << bits) - 1;
  int out_of_range;
  if (ps_node_integer_value_is_unsigned(lhs)) {
    out_of_range = value < 0 || value > max_unsigned;
    if (value < 0 && value >= min_signed) out_of_range = 0;
  } else {
    out_of_range = value < min_signed || value > max_signed;
  }
  if (out_of_range) {
    diag_warn_tokf_in(diagnostics,
        DIAG_WARN_PARSER_CONSTANT_OVERFLOW, tok,
        "整数リテラル %lld は %d バイト型に収まりません (値が切り詰められます)",
        value, type_size);
  }
}

static void warn_assignment(
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const token_t *fallback) {
  if (!node || node->kind != ND_ASSIGN ||
      (!node->is_source_assignment && !node->is_decl_initializer))
    return;
  node_t *lhs = node->lhs;
  node_t *rhs = node->rhs;
  const token_t *tok = node->tok ? node->tok : fallback;
  if (node->is_source_assignment && lhs && lhs->kind == ND_LVAR && rhs &&
      rhs->kind == ND_LVAR && ps_node_lvar_symbol(lhs) &&
      ps_node_lvar_symbol(lhs) == ps_node_lvar_symbol(rhs)) {
    diag_warn_tokf_in(diagnostics,
        DIAG_WARN_PARSER_SELF_ASSIGN, tok,
        "変数を自身に代入しています (タイプミスの可能性)");
  }
  if (lhs && rhs && !ps_node_value_is_pointer_like(lhs) &&
      node_fp_kind(lhs) == TK_FLOAT_KIND_NONE &&
      node_fp_kind(rhs) != TK_FLOAT_KIND_NONE) {
    if (node->is_decl_initializer) {
      warn_float_to_int(
          diagnostics, rhs, tok,
          "整数変数を浮動小数点リテラル %g で初期化しています (小数部が切り捨てられます)",
          "整数変数を浮動小数点値で初期化しています (小数部が切り捨てられます)");
    } else {
      warn_float_to_int(
          diagnostics, rhs, tok,
          "整数変数に浮動小数点リテラル %g を代入しています (小数部が切り捨てられます)",
          "整数変数に浮動小数点値を代入しています (小数部が切り捨てられます)");
    }
  }
  if (node->is_decl_initializer)
    warn_decl_initializer_overflow(diagnostics, lhs, rhs, tok);
}

static void warn_return(
    ag_diagnostic_context_t *diagnostics, node_t *node,
    node_function_definition_t *current_func,
    const token_t *fallback) {
  if (!node || node->kind != ND_RETURN || !node->lhs || !current_func)
    return;
  const psx_type_t *ret_type =
      ps_function_definition_return_type(current_func);
  tk_float_kind_t ret_fp = type_fp_kind(ret_type);
  int ret_pointer = ps_type_is_pointer(ret_type);
  int ret_void = ret_type && ret_type->kind == PSX_TYPE_VOID;
  const token_t *tok = node->tok ? node->tok : fallback;
  if (node_fp_kind(node->lhs) != TK_FLOAT_KIND_NONE &&
      ret_fp == TK_FLOAT_KIND_NONE && !ret_pointer && !ret_void) {
    warn_float_to_int(
        diagnostics, node->lhs, tok,
        "整数戻り型の関数から浮動小数点リテラル %g を return しています (小数部が切り捨てられます)",
        "整数戻り型の関数から浮動小数点値を return しています (小数部が切り捨てられます)");
  }
  if (ret_pointer && node->lhs->kind == ND_ADDR && node->lhs->lhs &&
      node->lhs->lhs->kind == ND_LVAR) {
    lvar_t *src = ps_node_lvar_symbol(node->lhs->lhs);
    if (src && !ps_lvar_is_static_local(src)) {
      diag_warn_tokf_in(diagnostics,
          DIAG_WARN_PARSER_RETURN_STACK_ADDRESS, tok,
          "ローカル変数 '%.*s' のアドレスを返しています (dangling pointer になります)",
          ps_lvar_name_len(src), ps_lvar_name(src));
    }
  }
}

static const char *source_op_text(token_kind_t op) {
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

static void source_compare_operands(
    node_t *node, node_t **out_lhs, node_t **out_rhs) {
  if (node && (node->source_op == TK_GT || node->source_op == TK_GE)) {
    *out_lhs = node->rhs;
    *out_rhs = node->lhs;
  } else {
    *out_lhs = node ? node->lhs : NULL;
    *out_rhs = node ? node->rhs : NULL;
  }
}

static int nodes_identity_equal(node_t *lhs, node_t *rhs) {
  if (!lhs || !rhs || lhs->kind != rhs->kind) return 0;
  if (lhs->kind == ND_LVAR) {
    lvar_t *var = ps_node_lvar_symbol(lhs);
    return var && var == ps_node_lvar_symbol(rhs);
  }
  if (lhs->kind == ND_GVAR) {
    node_gvar_t *a = (node_gvar_t *)lhs;
    node_gvar_t *b = (node_gvar_t *)rhs;
    return a->name_len == b->name_len &&
           memcmp(a->name, b->name, (size_t)a->name_len) == 0;
  }
  if (lhs->kind == ND_NUM) {
    return ((node_num_t *)lhs)->val == ((node_num_t *)rhs)->val &&
           node_fp_kind(lhs) == node_fp_kind(rhs);
  }
  return 0;
}

static void warn_identical_logical(
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const token_t *fallback) {
  if (!node || (node->source_op != TK_OROR && node->source_op != TK_ANDAND) ||
      !nodes_identity_equal(node->lhs, node->rhs))
    return;
  const char *op = source_op_text(node->source_op);
  diag_warn_tokf_in(diagnostics,
      DIAG_WARN_PARSER_IDENTICAL_LOGICAL_OPERANDS,
      node->tok ? node->tok : fallback,
      "'%s' の両辺が同じ式です (常に同じ結果、タイプミスの可能性)", op);
}

static void warn_sign_compare(
    ag_diagnostic_context_t *diagnostics, node_t *lhs, node_t *rhs,
    const char *op, const token_t *tok) {
  if (!lhs || !rhs) return;
  int lhs_unsigned = ps_node_integer_promotion_is_unsigned(lhs);
  int rhs_unsigned = ps_node_integer_promotion_is_unsigned(rhs);
  if (lhs_unsigned == rhs_unsigned) return;
  node_t *signed_side = lhs_unsigned ? rhs : lhs;
  if (signed_side->kind == ND_NUM &&
      ((node_num_t *)signed_side)->val >= 0)
    return;
  if (!ps_node_usual_arith_operands_is_unsigned(lhs, rhs)) return;
  diag_warn_tokf_in(diagnostics,
      DIAG_WARN_PARSER_SIGN_COMPARE, tok,
      "符号付きと符号なしの整数を比較しています ('%s' / 負値が大きな正の値として扱われる可能性)",
      op);
}

static int is_zero_literal(node_t *node) {
  return node && node->kind == ND_NUM &&
         node_fp_kind(node) == TK_FLOAT_KIND_NONE &&
         ((node_num_t *)node)->val == 0;
}

static void warn_unsigned_zero(
    ag_diagnostic_context_t *diagnostics, node_t *lhs, node_t *rhs,
    const char *op, const token_t *tok) {
  if (ps_node_integer_value_is_unsigned(lhs) && is_zero_literal(rhs)) {
    if (strcmp(op, ">=") == 0)
      diag_warn_tokf_in(diagnostics, DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, tok,
                     "符号なし整数は常に 0 以上です: '%s 0' は常に真", op);
    else if (strcmp(op, "<") == 0)
      diag_warn_tokf_in(diagnostics, DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, tok,
                     "符号なし整数は常に 0 以上です: '%s 0' は常に偽", op);
  }
  if (is_zero_literal(lhs) && ps_node_integer_value_is_unsigned(rhs)) {
    if (strcmp(op, "<=") == 0)
      diag_warn_tokf_in(diagnostics, DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, tok,
                     "符号なし整数は常に 0 以上です: '0 %s' は常に真", op);
    else if (strcmp(op, ">") == 0)
      diag_warn_tokf_in(diagnostics, DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, tok,
                     "符号なし整数は常に 0 以上です: '0 %s' は常に偽", op);
  }
}

static void warn_comparison(
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const token_t *fallback) {
  token_kind_t op_kind = node ? node->source_op : TK_EOF;
  if (op_kind != TK_EQEQ && op_kind != TK_NEQ && op_kind != TK_LT &&
      op_kind != TK_LE && op_kind != TK_GT && op_kind != TK_GE)
    return;
  node_t *lhs;
  node_t *rhs;
  source_compare_operands(node, &lhs, &rhs);
  const char *op = source_op_text(op_kind);
  const token_t *tok = node->tok ? node->tok : fallback;
  if (op_kind == TK_EQEQ || op_kind == TK_NEQ) {
    if (lhs && lhs->from_logical_not) {
      diag_warn_tokf_in(diagnostics,
          DIAG_WARN_PARSER_LOGICAL_NOT_PARENTHESES, tok,
          "'%s' の左辺が単項 '!' で、'!' の優先順位が '%s' より高いため "
          "'(!x) %s y' と解釈されます ('!(x %s y)' を意図していませんか)",
          op, op, op, op);
    }
    if (nodes_identity_equal(lhs, rhs))
      diag_warn_tokf_in(diagnostics, DIAG_WARN_PARSER_SELF_COMPARE, tok,
                     "自己比較 (常に同じ値): '%s'", op);
    warn_sign_compare(diagnostics, lhs, rhs, op, tok);
    node_t *pointer = NULL;
    node_t *number = NULL;
    if (lhs && rhs && ps_node_value_is_pointer_like(lhs) &&
        !ps_node_value_is_pointer_like(rhs) && rhs->kind == ND_NUM) {
      pointer = lhs;
      number = rhs;
    } else if (lhs && rhs && ps_node_value_is_pointer_like(rhs) &&
               !ps_node_value_is_pointer_like(lhs) && lhs->kind == ND_NUM) {
      pointer = rhs;
      number = lhs;
    }
    if (pointer && ((node_num_t *)number)->val != 0) {
      diag_warn_tokf_in(diagnostics,
          DIAG_WARN_PARSER_POINTER_INTEGER_COMPARE, tok,
          "ポインタを非ゼロ整数定数 (%lld) と '%s' で比較しています (C11 6.5.16.1)",
          ((node_num_t *)number)->val, op);
    }
  } else {
    warn_sign_compare(diagnostics, lhs, rhs, op, tok);
    warn_unsigned_zero(diagnostics, lhs, rhs, op, tok);
  }
}

static int is_plain_int_literal(node_t *node) {
  if (!node || node->kind != ND_NUM ||
      node_fp_kind(node) != TK_FLOAT_KIND_NONE ||
      ps_node_integer_value_is_unsigned(node))
    return 0;
  const psx_type_t *type = ps_node_get_type(node);
  node_num_t *number = (node_num_t *)node;
  return type && ps_type_sizeof(type) <= 4 && !type->is_long_long &&
         number->val >= -2147483648LL && number->val <= 2147483647LL;
}

static void warn_arithmetic(
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const token_t *fallback) {
  if (!node || node->source_op == TK_EOF) return;
  const token_t *tok = node->tok ? node->tok : fallback;
  if ((node->source_op == TK_PLUS || node->source_op == TK_MINUS ||
       node->source_op == TK_MUL) &&
      is_plain_int_literal(node->lhs) && is_plain_int_literal(node->rhs)) {
    long long lhs = ((node_num_t *)node->lhs)->val;
    long long rhs = ((node_num_t *)node->rhs)->val;
    long long result = node->source_op == TK_PLUS
                           ? lhs + rhs
                           : (node->source_op == TK_MINUS
                                  ? lhs - rhs
                                  : lhs * rhs);
    if (result < -2147483648LL || result > 2147483647LL) {
      diag_warn_tokf_in(diagnostics,
          DIAG_WARN_PARSER_INTEGER_OVERFLOW, tok,
          "整数定数式 %lld %s %lld = %lld は int の範囲を超えています (C11 6.5p5 未定義動作)",
          lhs, source_op_text(node->source_op), rhs, result);
    }
  }
  if ((node->source_op == TK_SHL || node->source_op == TK_SHR) &&
      node->rhs && node->rhs->kind == ND_NUM) {
    long long amount = ((node_num_t *)node->rhs)->val;
    int width = node->lhs && ps_node_type_size(node->lhs) >= 8 ? 64 : 32;
    if (amount < 0 || amount >= width) {
      diag_warn_tokf_in(diagnostics,
          DIAG_WARN_PARSER_SHIFT_OUT_OF_RANGE, tok,
          "シフト量 %lld が型の幅 (%d ビット) を超えています (C11 6.5.7p3 未定義動作): %s",
          amount, width, source_op_text(node->source_op));
    }
  }
  if ((node->source_op == TK_DIV || node->source_op == TK_MOD) &&
      node->rhs && node->rhs->kind == ND_NUM &&
      node_fp_kind(node->rhs) == TK_FLOAT_KIND_NONE &&
      ((node_num_t *)node->rhs)->val == 0) {
    diag_warn_tokf_in(diagnostics,
        DIAG_WARN_PARSER_DIVIDE_BY_ZERO, tok,
        node->source_op == TK_DIV
            ? "0 による除算 (C11 6.5.5p5 未定義動作)"
            : "0 による剰余 (C11 6.5.5p5 未定義動作)");
  }
}

static void warn_function(
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const token_t *fallback) {
  if (node->kind == ND_FUNCALL && node->is_implicit_func_decl) {
    node_function_call_t *call = (node_function_call_t *)node;
    if (call->direct_name) {
      diag_warn_tokf_in(diagnostics,
          DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL,
          node->tok ? node->tok : fallback,
          "関数 '%.*s' は宣言されていません (C99/C11 で implicit declaration は不可)",
          call->direct_name_len, call->direct_name);
    }
  } else if (node->kind == ND_FUNCDEF && node->is_implicit_int_return) {
    diag_warn_tokf_in(diagnostics,
        DIAG_WARN_PARSER_IMPLICIT_INT_RETURN,
        node->tok ? node->tok : fallback, "%s",
        diag_warn_message_for_in(diagnostics, DIAG_WARN_PARSER_IMPLICIT_INT_RETURN));
  }
}

static void warn_condition(
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const token_t *fallback) {
  if (!node || (node->kind != ND_IF && node->kind != ND_WHILE)) return;
  const char *context = node->kind == ND_IF ? "if 文" : "while 文";
  const token_t *tok = node->tok ? node->tok : fallback;
  if (node->lhs && node->lhs->kind == ND_ASSIGN) {
    diag_warn_tokf_in(diagnostics,
        DIAG_WARN_PARSER_ASSIGN_IN_CONDITION, tok,
        "%s の条件に代入式を使っています ('==' のタイプミスの可能性)", context);
  } else if (node->lhs && node->lhs->kind == ND_COMMA) {
    diag_warn_tokf_in(diagnostics,
        DIAG_WARN_PARSER_COMMA_IN_CONDITION, tok,
        "%s の条件にカンマ演算子を使っています ('&&' のタイプミスの可能性)", context);
  }
  if (node->kind == ND_IF && node->has_empty_body)
    diag_warn_tokf_in(diagnostics, DIAG_WARN_PARSER_EMPTY_BODY, tok,
                   "if 文の本体が空です (タイプミスの可能性)");
}

static void emit_node_warning(
    ag_diagnostic_context_t *diagnostics, node_t *node,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok) {
  if (!node) return;
  switch (node->kind) {
    case ND_ASSIGN:
      warn_assignment(diagnostics, node, fallback_diag_tok);
      break;
    case ND_RETURN:
      warn_return(diagnostics, node, current_func, fallback_diag_tok);
      break;
    case ND_LOGOR:
    case ND_LOGAND:
      warn_identical_logical(diagnostics, node, fallback_diag_tok);
      break;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
      warn_comparison(diagnostics, node, fallback_diag_tok);
      break;
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_SHL:
    case ND_SHR:
      warn_arithmetic(diagnostics, node, fallback_diag_tok);
      break;
    case ND_FUNCDEF:
    case ND_FUNCALL:
      warn_function(diagnostics, node, fallback_diag_tok);
      break;
    case ND_IF:
    case ND_WHILE:
      warn_condition(diagnostics, node, fallback_diag_tok);
      break;
    default:
      break;
  }
}

static void emit_warning_tree(
    ag_diagnostic_context_t *diagnostics, node_t *node,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok);

static void emit_warning_array(
    ag_diagnostic_context_t *diagnostics, node_t **nodes,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++)
    emit_warning_tree(
        diagnostics, nodes[i], current_func, fallback_diag_tok);
}

static void emit_warning_tree(
    ag_diagnostic_context_t *diagnostics, node_t *node,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok) {
  if (!node) return;
  emit_node_warning(diagnostics, node, current_func, fallback_diag_tok);
  switch (node->kind) {
    case ND_BLOCK:
      emit_warning_array(
          diagnostics, ((node_block_t *)node)->body, current_func,
          fallback_diag_tok);
      return;
    case ND_FUNCDEF: {
      node_function_definition_t *function =
          (node_function_definition_t *)node;
      emit_warning_array(
          diagnostics, function->parameters, function, fallback_diag_tok);
      emit_warning_tree(
          diagnostics, node->rhs, function, fallback_diag_tok);
      return;
    }
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      emit_warning_tree(
          diagnostics, call->callee, current_func, fallback_diag_tok);
      for (int i = 0; i < call->argument_count; i++)
        emit_warning_tree(
            diagnostics, call->arguments[i], current_func,
            fallback_diag_tok);
      return;
    }
    case ND_IF:
    case ND_WHILE:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      emit_warning_tree(
          diagnostics, control->init, current_func, fallback_diag_tok);
      emit_warning_tree(
          diagnostics, node->lhs, current_func, fallback_diag_tok);
      emit_warning_tree(
          diagnostics, node->rhs, current_func, fallback_diag_tok);
      emit_warning_tree(
          diagnostics, control->inc, current_func, fallback_diag_tok);
      emit_warning_tree(
          diagnostics, control->els, current_func, fallback_diag_tok);
      return;
    }
    default:
      emit_warning_tree(
          diagnostics, node->lhs, current_func, fallback_diag_tok);
      emit_warning_tree(
          diagnostics, node->rhs, current_func, fallback_diag_tok);
      return;
  }
}

void psx_emit_semantic_warnings(
    ag_diagnostic_context_t *diagnostics, node_t *root,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok) {
  emit_warning_tree(
      diagnostics, root, current_func, fallback_diag_tok);
}
