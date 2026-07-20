#include "semantic_diagnostics.h"
#include "function_call_resolution.h"

#include "../diag/diag.h"
#include "../parser/lvar_public.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../type_layout.h"
#include "resolved_node_kind.h"
#include "resolved_object_ref.h"
#include "type_identity.h"

#include <stdio.h>
#include <string.h>

static int fp_literal_fractional_part_known(double value) {
  if (value < -2147483648.0 || value > 2147483647.0) return 0;
  return value != (double)(long long)value;
}

static tk_float_kind_t node_fp_kind(
    const psx_resolution_store_t *store, node_t *node) {
  psx_type_shape_t type = {0};
  if (!node || !ps_node_type_shape(store, node, &type) ||
      (type.kind != PSX_TYPE_FLOAT && type.kind != PSX_TYPE_COMPLEX))
    return TK_FLOAT_KIND_NONE;
  if (type.floating_kind == PSX_FLOATING_KIND_FLOAT)
    return TK_FLOAT_KIND_FLOAT;
  if (type.floating_kind == PSX_FLOATING_KIND_LONG_DOUBLE)
    return TK_FLOAT_KIND_LONG_DOUBLE;
  return TK_FLOAT_KIND_DOUBLE;
}

static tk_float_kind_t semantic_fp_kind(
    const psx_semantic_type_table_t *types, psx_qual_type_t type) {
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(types, type.type_id, &shape) ||
      (shape.kind != PSX_TYPE_FLOAT && shape.kind != PSX_TYPE_COMPLEX))
    return TK_FLOAT_KIND_NONE;
  switch (shape.floating_kind) {
    case PSX_FLOATING_KIND_FLOAT: return TK_FLOAT_KIND_FLOAT;
    case PSX_FLOATING_KIND_LONG_DOUBLE: return TK_FLOAT_KIND_LONG_DOUBLE;
    case PSX_FLOATING_KIND_DOUBLE: return TK_FLOAT_KIND_DOUBLE;
    case PSX_FLOATING_KIND_NONE: return TK_FLOAT_KIND_DOUBLE;
    default: return TK_FLOAT_KIND_NONE;
  }
}

static psx_resolution_node_kind_t resolved_node_kind(
    const psx_resolution_store_t *store, const node_t *node) {
  return psx_resolved_object_ref_node_kind(store, node);
}

static int semantic_type_size(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t qual_type) {
  return ps_type_sizeof_id(ps_ctx_semantic_type_table_in(semantic_context),
                           ps_ctx_record_layout_table_in(semantic_context),
                           qual_type.type_id,
                           ps_ctx_data_layout(semantic_context));
}

static void warn_float_to_int(
    const psx_resolution_store_t *store,
    ag_diagnostic_context_t *diagnostics, node_t *value,
    const token_t *tok) {
  if (!value || node_fp_kind(store, value) == TK_FLOAT_KIND_NONE) return;
  char detail[64] = "";
  if (value->kind == ND_NUM) {
    double f = ((node_num_t *)value)->fval;
    if (!fp_literal_fractional_part_known(f)) return;
    snprintf(detail, sizeof(detail), " (%g)", f);
  }
  diag_warn_tokf_in(diagnostics,
      DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING, tok,
      diag_warn_message_for_in(
          diagnostics, DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING),
      detail);
}

static void warn_decl_initializer_overflow(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *lhs, node_t *rhs,
    const token_t *tok) {
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  if (!lhs || !rhs || resolved_node_kind(store, lhs) != ND_LVAR ||
      rhs->kind != ND_NUM)
    return;
  psx_type_shape_t lhs_type = {0};
  int has_lhs_type = ps_node_type_shape(store, lhs, &lhs_type);
  if (node_fp_kind(store, lhs) != TK_FLOAT_KIND_NONE ||
      node_fp_kind(store, rhs) != TK_FLOAT_KIND_NONE ||
      ps_node_value_is_pointer_like(store, lhs) ||
      (has_lhs_type && psx_type_kind_is_aggregate(lhs_type.kind)))
    return;
  if (has_lhs_type && lhs_type.kind == PSX_TYPE_BOOL) return;
  int type_size = semantic_type_size(
      semantic_context, ps_node_qual_type(store, lhs));
  if (type_size <= 0 || type_size >= 4) return;

  long long value = ((node_num_t *)rhs)->val;
  int bits = type_size * 8;
  long long max_signed = (1LL << (bits - 1)) - 1;
  long long min_signed = -(1LL << (bits - 1));
  long long max_unsigned = (1LL << bits) - 1;
  int out_of_range;
  if (ps_node_integer_value_is_unsigned(store, lhs)) {
    out_of_range = value < 0 || value > max_unsigned;
    if (value < 0 && value >= min_signed) out_of_range = 0;
  } else {
    out_of_range = value < min_signed || value > max_signed;
  }
  if (out_of_range) {
    diag_warn_tokf_in(diagnostics,
        DIAG_WARN_PARSER_CONSTANT_OVERFLOW, tok,
        diag_warn_message_for_in(
            diagnostics, DIAG_WARN_PARSER_CONSTANT_OVERFLOW),
        value, type_size);
  }
}

static void warn_assignment(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const token_t *fallback) {
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  if (!node || node->kind != ND_ASSIGN ||
      (!ps_node_is_source_assignment(store, node) &&
       !ps_node_is_decl_initializer(store, node)))
    return;
  node_t *lhs = node->lhs;
  node_t *rhs = node->rhs;
  const token_t *tok = node->tok ? node->tok : fallback;
  if (ps_node_is_source_assignment(store, node) && lhs &&
      resolved_node_kind(store, lhs) == ND_LVAR && rhs &&
      resolved_node_kind(store, rhs) == ND_LVAR &&
      ps_node_lvar_symbol(store, lhs) &&
      ps_node_lvar_symbol(store, lhs) ==
          ps_node_lvar_symbol(store, rhs)) {
    diag_warn_tokf_in(diagnostics,
        DIAG_WARN_PARSER_SELF_ASSIGN, tok,
        "%s", diag_warn_message_for_in(
            diagnostics, DIAG_WARN_PARSER_SELF_ASSIGN));
  }
  if (lhs && rhs && !ps_node_value_is_pointer_like(store, lhs) &&
      node_fp_kind(store, lhs) == TK_FLOAT_KIND_NONE &&
      node_fp_kind(store, rhs) != TK_FLOAT_KIND_NONE) {
    if (ps_node_is_decl_initializer(store, node)) {
      warn_float_to_int(store, diagnostics, rhs, tok);
    } else {
      warn_float_to_int(store, diagnostics, rhs, tok);
    }
  }
  if (ps_node_is_decl_initializer(store, node))
    warn_decl_initializer_overflow(
        semantic_context, diagnostics, lhs, rhs, tok);
}

static void warn_return(
    psx_semantic_context_t *semantic_context,
    const psx_resolution_store_t *store,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    node_function_definition_t *current_func,
    const token_t *fallback) {
  if (!node || node->kind != ND_RETURN || !node->lhs || !current_func)
    return;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_qual_type_t return_qual_type =
      ps_function_definition_return_qual_type(types, current_func);
  psx_type_shape_t return_shape = {0};
  int has_return_shape = psx_semantic_type_table_describe(
      types, return_qual_type.type_id, &return_shape);
  tk_float_kind_t ret_fp = semantic_fp_kind(types, return_qual_type);
  int ret_pointer = has_return_shape &&
                    return_shape.kind == PSX_TYPE_POINTER;
  int ret_void = has_return_shape && return_shape.kind == PSX_TYPE_VOID;
  const token_t *tok = node->tok ? node->tok : fallback;
  if (node_fp_kind(store, node->lhs) != TK_FLOAT_KIND_NONE &&
      ret_fp == TK_FLOAT_KIND_NONE && !ret_pointer && !ret_void) {
    warn_float_to_int(store, diagnostics, node->lhs, tok);
  }
  if (ret_pointer &&
      (node->lhs->kind == ND_ADDRESS_OF ||
       resolved_node_kind(store, node->lhs) == ND_ADDR) &&
      node->lhs->lhs &&
      resolved_node_kind(store, node->lhs->lhs) == ND_LVAR) {
    lvar_t *src = ps_node_lvar_symbol(store, node->lhs->lhs);
    if (src && !ps_lvar_is_static_local(src)) {
      diag_warn_tokf_in(diagnostics,
          DIAG_WARN_PARSER_RETURN_STACK_ADDRESS, tok,
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_RETURN_STACK_ADDRESS),
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

static int nodes_identity_equal(
    const psx_resolution_store_t *store, node_t *lhs, node_t *rhs) {
  if (!lhs || !rhs ||
      resolved_node_kind(store, lhs) != resolved_node_kind(store, rhs))
    return 0;
  psx_resolved_object_ref_kind_t reference_kind =
      psx_resolved_object_ref_kind(store, lhs);
  if (reference_kind != psx_resolved_object_ref_kind(store, rhs)) return 0;
  if (reference_kind == PSX_RESOLVED_OBJECT_REF_LOCAL) {
    lvar_t *var = psx_resolved_object_ref_local(store, lhs);
    return var && var == psx_resolved_object_ref_local(store, rhs);
  }
  if (reference_kind == PSX_RESOLVED_OBJECT_REF_GLOBAL ||
      reference_kind == PSX_RESOLVED_OBJECT_REF_FUNCTION) {
    int lhs_len = 0;
    int rhs_len = 0;
    char *lhs_name =
        psx_resolved_object_ref_name(store, lhs, &lhs_len);
    char *rhs_name =
        psx_resolved_object_ref_name(store, rhs, &rhs_len);
    return lhs_name && rhs_name && lhs_len == rhs_len &&
           memcmp(lhs_name, rhs_name, (size_t)lhs_len) == 0;
  }
  if (lhs->kind == ND_NUM) {
    return ((node_num_t *)lhs)->val == ((node_num_t *)rhs)->val &&
           node_fp_kind(store, lhs) == node_fp_kind(store, rhs);
  }
  return 0;
}

static void warn_identical_logical(
    const psx_resolution_store_t *store,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const token_t *fallback) {
  if (!node || (node->source_op != TK_OROR && node->source_op != TK_ANDAND) ||
      !nodes_identity_equal(store, node->lhs, node->rhs))
    return;
  const char *op = source_op_text(node->source_op);
  diag_warn_tokf_in(diagnostics,
      DIAG_WARN_PARSER_IDENTICAL_LOGICAL_OPERANDS,
      node->tok ? node->tok : fallback,
      diag_warn_message_for_in(
          diagnostics, DIAG_WARN_PARSER_IDENTICAL_LOGICAL_OPERANDS),
      op);
}

static void warn_sign_compare(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *lhs, node_t *rhs,
    const char *op, const token_t *tok) {
  if (!lhs || !rhs) return;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  const ag_data_layout_t *data_layout = ps_ctx_data_layout(semantic_context);
  int lhs_unsigned = ps_type_integer_promotion_is_unsigned_for_data_layout(
      ps_node_get_type(store, lhs), data_layout);
  int rhs_unsigned = ps_type_integer_promotion_is_unsigned_for_data_layout(
      ps_node_get_type(store, rhs), data_layout);
  if (lhs_unsigned == rhs_unsigned) return;
  node_t *signed_side = lhs_unsigned ? rhs : lhs;
  if (signed_side->kind == ND_NUM &&
      ((node_num_t *)signed_side)->val >= 0)
    return;
  if (!ps_type_usual_arithmetic_result_is_unsigned_for_data_layout(
          ps_node_get_type(store, lhs), ps_node_get_type(store, rhs),
          data_layout)) {
    return;
  }
  diag_warn_tokf_in(diagnostics,
      DIAG_WARN_PARSER_SIGN_COMPARE, tok,
      diag_warn_message_for_in(
          diagnostics, DIAG_WARN_PARSER_SIGN_COMPARE),
      op);
}

static int is_zero_literal(
    const psx_resolution_store_t *store, node_t *node) {
  return node && node->kind == ND_NUM &&
         node_fp_kind(store, node) == TK_FLOAT_KIND_NONE &&
         ((node_num_t *)node)->val == 0;
}

static void warn_unsigned_zero(
    const psx_resolution_store_t *store,
    ag_diagnostic_context_t *diagnostics, node_t *lhs, node_t *rhs,
    const char *op, const token_t *tok) {
  if (ps_node_integer_value_is_unsigned(store, lhs) &&
      is_zero_literal(store, rhs)) {
    if (strcmp(op, ">=") == 0)
      diag_warn_tokf_in(diagnostics, DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, tok,
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO),
          op, 1);
    else if (strcmp(op, "<") == 0)
      diag_warn_tokf_in(diagnostics, DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, tok,
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO),
          op, 0);
  }
  if (is_zero_literal(store, lhs) &&
      ps_node_integer_value_is_unsigned(store, rhs)) {
    if (strcmp(op, "<=") == 0)
      diag_warn_tokf_in(diagnostics, DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, tok,
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO),
          op, 1);
    else if (strcmp(op, ">") == 0)
      diag_warn_tokf_in(diagnostics, DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, tok,
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO),
          op, 0);
  }
}

static void warn_comparison(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const token_t *fallback) {
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  token_kind_t op_kind = node ? node->source_op : TK_EOF;
  if (op_kind != TK_EQEQ && op_kind != TK_NEQ && op_kind != TK_LT &&
      op_kind != TK_LE && op_kind != TK_GT && op_kind != TK_GE)
    return;
  node_t *lhs = node ? node->lhs : NULL;
  node_t *rhs = node ? node->rhs : NULL;
  const char *op = source_op_text(op_kind);
  const token_t *tok = node->tok ? node->tok : fallback;
  if (op_kind == TK_EQEQ || op_kind == TK_NEQ) {
    if (lhs && lhs->kind == ND_LOGICAL_NOT) {
      diag_warn_tokf_in(diagnostics,
          DIAG_WARN_PARSER_LOGICAL_NOT_PARENTHESES, tok,
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_LOGICAL_NOT_PARENTHESES),
          op, op, op, op);
    }
    if (nodes_identity_equal(store, lhs, rhs))
      diag_warn_tokf_in(diagnostics, DIAG_WARN_PARSER_SELF_COMPARE, tok,
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_SELF_COMPARE),
          op);
    warn_sign_compare(
        semantic_context, diagnostics, lhs, rhs, op, tok);
    node_t *pointer = NULL;
    node_t *number = NULL;
    if (lhs && rhs && ps_node_value_is_pointer_like(store, lhs) &&
        !ps_node_value_is_pointer_like(store, rhs) &&
        rhs->kind == ND_NUM) {
      pointer = lhs;
      number = rhs;
    } else if (lhs && rhs && ps_node_value_is_pointer_like(store, rhs) &&
               !ps_node_value_is_pointer_like(store, lhs) &&
               lhs->kind == ND_NUM) {
      pointer = rhs;
      number = lhs;
    }
    if (pointer && ((node_num_t *)number)->val != 0) {
      diag_warn_tokf_in(diagnostics,
          DIAG_WARN_PARSER_POINTER_INTEGER_COMPARE, tok,
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_POINTER_INTEGER_COMPARE),
          ((node_num_t *)number)->val, op);
    }
  } else {
    warn_sign_compare(
        semantic_context, diagnostics, lhs, rhs, op, tok);
    warn_unsigned_zero(store, diagnostics, lhs, rhs, op, tok);
  }
}

static int is_plain_int_literal(
    const psx_resolution_store_t *store, node_t *node) {
  if (!node || node->kind != ND_NUM ||
      node_fp_kind(store, node) != TK_FLOAT_KIND_NONE ||
      ps_node_integer_value_is_unsigned(store, node))
    return 0;
  psx_type_shape_t type = {0};
  node_num_t *number = (node_num_t *)node;
  return ps_node_type_shape(store, node, &type) &&
         type.kind == PSX_TYPE_INTEGER &&
         type.integer_kind == PSX_INTEGER_KIND_INT &&
         number->val >= -2147483648LL && number->val <= 2147483647LL;
}

static void warn_arithmetic(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const token_t *fallback) {
  if (!node || node->source_op == TK_EOF) return;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  const token_t *tok = node->tok ? node->tok : fallback;
  if ((node->source_op == TK_PLUS || node->source_op == TK_MINUS ||
       node->source_op == TK_MUL) &&
      is_plain_int_literal(store, node->lhs) &&
      is_plain_int_literal(store, node->rhs)) {
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
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_INTEGER_OVERFLOW),
          lhs, source_op_text(node->source_op), rhs, result);
    }
  }
  if ((node->source_op == TK_SHL || node->source_op == TK_SHR) &&
      node->rhs && node->rhs->kind == ND_NUM) {
    long long amount = ((node_num_t *)node->rhs)->val;
    int width = node->lhs &&
                        semantic_type_size(
                            semantic_context,
                            ps_node_qual_type(store, node->lhs)) >= 8
                    ? 64
                    : 32;
    if (amount < 0 || amount >= width) {
      diag_warn_tokf_in(diagnostics,
          DIAG_WARN_PARSER_SHIFT_OUT_OF_RANGE, tok,
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_SHIFT_OUT_OF_RANGE),
          amount, width, source_op_text(node->source_op));
    }
  }
  if ((node->source_op == TK_DIV || node->source_op == TK_MOD) &&
      node->rhs && node->rhs->kind == ND_NUM &&
      node_fp_kind(store, node->rhs) == TK_FLOAT_KIND_NONE &&
      ((node_num_t *)node->rhs)->val == 0) {
    diag_warn_tokf_in(diagnostics,
        DIAG_WARN_PARSER_DIVIDE_BY_ZERO, tok,
        diag_warn_message_for_in(
            diagnostics, DIAG_WARN_PARSER_DIVIDE_BY_ZERO),
        source_op_text(node->source_op));
  }
}

static void warn_function(
    const psx_resolution_store_t *store,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const token_t *fallback) {
  if (node->kind == ND_FUNCALL &&
      psx_function_call_is_implicit_declaration(
          store, (node_function_call_t *)node)) {
    node_function_call_t *call = (node_function_call_t *)node;
    const char *direct_name =
        psx_function_call_direct_name(store, call);
    if (direct_name) {
      diag_warn_tokf_in(diagnostics,
          DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL,
          node->tok ? node->tok : fallback,
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL),
          psx_function_call_direct_name_length(store, call), direct_name);
    }
  } else if (resolved_node_kind(store, node) == ND_FUNCDEF &&
             ps_node_is_implicit_int_return(store, node)) {
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
  const char *context = node->kind == ND_IF ? "if" : "while";
  const token_t *tok = node->tok ? node->tok : fallback;
  if (node->lhs &&
      (node->lhs->kind == ND_ASSIGN ||
       node->lhs->kind == ND_COMPOUND_ASSIGN)) {
    diag_warn_tokf_in(diagnostics,
        DIAG_WARN_PARSER_ASSIGN_IN_CONDITION, tok,
        diag_warn_message_for_in(
            diagnostics, DIAG_WARN_PARSER_ASSIGN_IN_CONDITION),
        context);
  } else if (node->lhs && node->lhs->kind == ND_COMMA) {
    diag_warn_tokf_in(diagnostics,
        DIAG_WARN_PARSER_COMMA_IN_CONDITION, tok,
        diag_warn_message_for_in(
            diagnostics, DIAG_WARN_PARSER_COMMA_IN_CONDITION),
        context);
  }
  if (node->kind == ND_IF && node->rhs &&
      node->rhs->kind == ND_NULL_STMT)
    diag_warn_tokf_in(diagnostics, DIAG_WARN_PARSER_EMPTY_BODY, tok,
        "%s", diag_warn_message_for_in(
            diagnostics, DIAG_WARN_PARSER_EMPTY_BODY));
}

static void emit_node_warning(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok) {
  if (!node) return;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  switch (resolved_node_kind(store, node)) {
    case ND_ASSIGN:
      warn_assignment(
          semantic_context, diagnostics, node, fallback_diag_tok);
      break;
    case ND_RETURN:
      warn_return(
          semantic_context, store, diagnostics, node,
          current_func, fallback_diag_tok);
      break;
    case ND_LOGOR:
    case ND_LOGAND:
      warn_identical_logical(
          store, diagnostics, node, fallback_diag_tok);
      break;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_GT:
    case ND_GE:
      warn_comparison(
          semantic_context, diagnostics, node, fallback_diag_tok);
      break;
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_SHL:
    case ND_SHR:
      warn_arithmetic(
          semantic_context, diagnostics, node, fallback_diag_tok);
      break;
    case ND_FUNCDEF:
    case ND_FUNCALL:
      warn_function(store, diagnostics, node, fallback_diag_tok);
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
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok);

static void emit_warning_array(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t **nodes,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++)
    emit_warning_tree(
        semantic_context, diagnostics, nodes[i], current_func,
        fallback_diag_tok);
}

static void emit_warning_tree(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok) {
  if (!node) return;
  emit_node_warning(
      semantic_context, diagnostics, node, current_func,
      fallback_diag_tok);
  switch (resolved_node_kind(
      ps_ctx_resolution_store(semantic_context), node)) {
    case ND_BLOCK:
      emit_warning_array(
          semantic_context, diagnostics, ((node_block_t *)node)->body,
          current_func, fallback_diag_tok);
      return;
    case ND_FUNCDEF: {
      node_function_definition_t *function =
          (node_function_definition_t *)node;
      emit_warning_array(
          semantic_context, diagnostics, function->parameters, function,
          fallback_diag_tok);
      emit_warning_tree(
          semantic_context, diagnostics, node->rhs, function,
          fallback_diag_tok);
      return;
    }
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      emit_warning_tree(
          semantic_context, diagnostics, call->callee, current_func,
          fallback_diag_tok);
      for (int i = 0; i < call->argument_count; i++)
        emit_warning_tree(
            semantic_context, diagnostics, call->arguments[i],
            current_func, fallback_diag_tok);
      return;
    }
    case ND_IF:
    case ND_WHILE:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      emit_warning_tree(
          semantic_context, diagnostics, control->init, current_func,
          fallback_diag_tok);
      emit_warning_tree(
          semantic_context, diagnostics, node->lhs, current_func,
          fallback_diag_tok);
      emit_warning_tree(
          semantic_context, diagnostics, node->rhs, current_func,
          fallback_diag_tok);
      emit_warning_tree(
          semantic_context, diagnostics, control->inc, current_func,
          fallback_diag_tok);
      emit_warning_tree(
          semantic_context, diagnostics, control->els, current_func,
          fallback_diag_tok);
      return;
    }
    default:
      emit_warning_tree(
          semantic_context, diagnostics, node->lhs, current_func,
          fallback_diag_tok);
      emit_warning_tree(
          semantic_context, diagnostics, node->rhs, current_func,
          fallback_diag_tok);
      return;
  }
}

void psx_emit_semantic_warnings(
    psx_semantic_context_t *semantic_context, node_t *root,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok) {
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  emit_warning_tree(
      semantic_context, diagnostics, root, current_func,
      fallback_diag_tok);
}
