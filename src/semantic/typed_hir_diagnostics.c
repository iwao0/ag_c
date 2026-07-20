#include "typed_hir_diagnostics.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../diag/diag.h"
#include "../parser/semantic_ctx.h"
#include "../type_layout.h"
#include "semantic_node_builder.h"
#include "semantic_node_internal.h"
#include "typed_hir_tree_internal.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  ag_diagnostic_context_t *diagnostics;
  const token_t *fallback_diag_tok;
  psx_qual_type_t function_return_type;
} typed_hir_diagnostic_walk_t;

static int type_is_floating(
    const typed_hir_diagnostic_walk_t *walk, psx_qual_type_t qual_type) {
  const psx_type_t *type = ps_ctx_type_by_id_in(
      walk->semantic_context, qual_type.type_id);
  return type && !ps_type_is_pointer(type) &&
         (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX);
}

static int type_is_integer(
    const typed_hir_diagnostic_walk_t *walk, psx_qual_type_t qual_type) {
  const psx_type_t *type = ps_ctx_type_by_id_in(
      walk->semantic_context, qual_type.type_id);
  return type && !ps_type_is_pointer(type) &&
         (type->kind == PSX_TYPE_BOOL || type->kind == PSX_TYPE_INTEGER);
}

static const psx_type_t *canonical_type(
    const typed_hir_diagnostic_walk_t *walk, psx_qual_type_t qual_type) {
  return walk && walk->semantic_context
             ? ps_ctx_type_by_id_in(
                   walk->semantic_context, qual_type.type_id)
             : NULL;
}

static int canonical_type_size(
    const typed_hir_diagnostic_walk_t *walk,
    psx_qual_type_t qual_type) {
  if (!walk || !walk->semantic_context ||
      qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  return ps_type_sizeof_id(
      ps_ctx_semantic_type_table_in(walk->semantic_context),
      ps_ctx_record_layout_table_in(walk->semantic_context), qual_type.type_id,
      ps_ctx_data_layout(walk->semantic_context));
}

static int type_is_pointer_like(
    const typed_hir_diagnostic_walk_t *walk, psx_qual_type_t qual_type) {
  const psx_type_t *type = canonical_type(walk, qual_type);
  return type && (type->kind == PSX_TYPE_POINTER ||
                  type->kind == PSX_TYPE_ARRAY ||
                  type->kind == PSX_TYPE_FUNCTION);
}

static const psx_semantic_node_t *child_with_edge(
    const psx_semantic_node_t *node, psx_hir_edge_kind_t edge) {
  if (!node) return NULL;
  for (size_t i = 0; i < node->spec.child_count; i++) {
    if (node->child_edges[i] == edge) return node->children[i];
  }
  return NULL;
}

static int known_integral_floating_literal(
    const typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *node) {
  if (!node || node->spec.kind != PSX_HIR_NUMBER ||
      !type_is_floating(
          walk, psx_semantic_node_expression_qual_type(node)))
    return 0;
  double value = node->spec.floating_value;
  if (value < -2147483648.0 || value > 2147483647.0) return 0;
  return value == (double)(long long)value;
}

static int node_identity_equal(
    const typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *lhs,
    const psx_semantic_node_t *rhs) {
  if (!lhs || !rhs || lhs->spec.kind != rhs->spec.kind) return 0;
  switch (lhs->spec.kind) {
    case PSX_HIR_LOCAL:
      return lhs->spec.storage_offset == rhs->spec.storage_offset &&
             lhs->spec.name_length == rhs->spec.name_length &&
             lhs->spec.name && rhs->spec.name &&
             memcmp(lhs->spec.name, rhs->spec.name,
                    lhs->spec.name_length) == 0;
    case PSX_HIR_GLOBAL:
    case PSX_HIR_FUNCTION_REF:
      return lhs->spec.name_length == rhs->spec.name_length &&
             lhs->spec.name && rhs->spec.name &&
             memcmp(lhs->spec.name, rhs->spec.name,
                    lhs->spec.name_length) == 0;
    case PSX_HIR_NUMBER:
      if (type_is_floating(
              walk, psx_semantic_node_expression_qual_type(lhs)) !=
          type_is_floating(
              walk, psx_semantic_node_expression_qual_type(rhs)))
        return 0;
      return type_is_floating(
                 walk, psx_semantic_node_expression_qual_type(lhs))
                 ? lhs->spec.floating_value == rhs->spec.floating_value
                 : lhs->spec.integer_value == rhs->spec.integer_value;
    default:
      return 0;
  }
}

static int integer_literal_value(
    const typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *node, long long *value) {
  if (!node || node->spec.kind != PSX_HIR_NUMBER ||
      !type_is_integer(
          walk, psx_semantic_node_expression_qual_type(node)))
    return 0;
  if (value) *value = node->spec.integer_value;
  return 1;
}

static const char *binary_operator_text(psx_hir_node_kind_t kind) {
  switch (kind) {
    case PSX_HIR_ADD: return "+";
    case PSX_HIR_SUB: return "-";
    case PSX_HIR_MUL: return "*";
    case PSX_HIR_DIV: return "/";
    case PSX_HIR_MOD: return "%";
    case PSX_HIR_SHL: return "<<";
    case PSX_HIR_SHR: return ">>";
    case PSX_HIR_EQ: return "==";
    case PSX_HIR_NE: return "!=";
    case PSX_HIR_LT: return "<";
    case PSX_HIR_LE: return "<=";
    case PSX_HIR_GT: return ">";
    case PSX_HIR_GE: return ">=";
    case PSX_HIR_LOGAND: return "&&";
    case PSX_HIR_LOGOR: return "||";
    default: return "";
  }
}

static void warn_self_or_logical_compare(
    typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *node,
    const psx_semantic_node_t *lhs,
    const psx_semantic_node_t *rhs) {
  if (!node_identity_equal(walk, lhs, rhs)) return;
  const char *op = binary_operator_text(node->spec.kind);
  if (node->spec.kind == PSX_HIR_LOGAND ||
      node->spec.kind == PSX_HIR_LOGOR) {
    diag_warn_tokf_in(
        walk->diagnostics,
        DIAG_WARN_PARSER_IDENTICAL_LOGICAL_OPERANDS,
        walk->fallback_diag_tok,
        diag_warn_message_for_in(
            walk->diagnostics,
            DIAG_WARN_PARSER_IDENTICAL_LOGICAL_OPERANDS),
        op);
  } else if (node->spec.kind == PSX_HIR_EQ ||
             node->spec.kind == PSX_HIR_NE) {
    diag_warn_tokf_in(
        walk->diagnostics, DIAG_WARN_PARSER_SELF_COMPARE,
        walk->fallback_diag_tok,
        diag_warn_message_for_in(
            walk->diagnostics, DIAG_WARN_PARSER_SELF_COMPARE),
        op);
  }
}

static void warn_sign_compare(
    typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *node,
    const psx_semantic_node_t *lhs,
    const psx_semantic_node_t *rhs) {
  const psx_type_t *lhs_type = lhs
      ? canonical_type(walk, psx_semantic_node_expression_qual_type(lhs))
      : NULL;
  const psx_type_t *rhs_type = rhs
      ? canonical_type(walk, psx_semantic_node_expression_qual_type(rhs))
      : NULL;
  if (!lhs_type || !rhs_type) return;
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(walk->semantic_context);
  int lhs_unsigned = ps_type_integer_promotion_is_unsigned_for_data_layout(
      lhs_type, data_layout);
  int rhs_unsigned = ps_type_integer_promotion_is_unsigned_for_data_layout(
      rhs_type, data_layout);
  if (lhs_unsigned == rhs_unsigned) return;
  const psx_semantic_node_t *signed_side = lhs_unsigned ? rhs : lhs;
  long long signed_literal = 0;
  if (integer_literal_value(walk, signed_side, &signed_literal) &&
      signed_literal >= 0)
    return;
  if (!ps_type_usual_arithmetic_result_is_unsigned_for_data_layout(
          lhs_type, rhs_type, data_layout))
    return;
  diag_warn_tokf_in(
      walk->diagnostics, DIAG_WARN_PARSER_SIGN_COMPARE,
      walk->fallback_diag_tok,
      diag_warn_message_for_in(
          walk->diagnostics, DIAG_WARN_PARSER_SIGN_COMPARE),
      binary_operator_text(node->spec.kind));
}

static void warn_unsigned_zero(
    typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *node,
    const psx_semantic_node_t *lhs,
    const psx_semantic_node_t *rhs) {
  long long lhs_value = 1;
  long long rhs_value = 1;
  int lhs_zero = integer_literal_value(walk, lhs, &lhs_value) &&
                 lhs_value == 0;
  int rhs_zero = integer_literal_value(walk, rhs, &rhs_value) &&
                 rhs_value == 0;
  const psx_type_t *lhs_type = lhs
      ? canonical_type(walk, psx_semantic_node_expression_qual_type(lhs))
      : NULL;
  const psx_type_t *rhs_type = rhs
      ? canonical_type(walk, psx_semantic_node_expression_qual_type(rhs))
      : NULL;
  const char *op = binary_operator_text(node->spec.kind);
  int always_true = 0;
  int warn = 0;
  if (rhs_zero && ps_type_is_unsigned(lhs_type)) {
    warn = node->spec.kind == PSX_HIR_LT ||
           node->spec.kind == PSX_HIR_GE;
    always_true = node->spec.kind == PSX_HIR_GE;
  } else if (lhs_zero && ps_type_is_unsigned(rhs_type)) {
    warn = node->spec.kind == PSX_HIR_LE ||
           node->spec.kind == PSX_HIR_GT;
    always_true = node->spec.kind == PSX_HIR_LE;
  }
  if (warn)
    diag_warn_tokf_in(
        walk->diagnostics,
        DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO,
        walk->fallback_diag_tok,
        diag_warn_message_for_in(
            walk->diagnostics,
            DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO),
        op, always_true);
}

static void warn_pointer_integer_compare(
    typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *node,
    const psx_semantic_node_t *lhs,
    const psx_semantic_node_t *rhs) {
  const psx_semantic_node_t *number = NULL;
  if (lhs && rhs &&
      type_is_pointer_like(
          walk, psx_semantic_node_expression_qual_type(lhs)) &&
      !type_is_pointer_like(
          walk, psx_semantic_node_expression_qual_type(rhs)))
    number = rhs;
  else if (lhs && rhs &&
           type_is_pointer_like(
               walk, psx_semantic_node_expression_qual_type(rhs)) &&
           !type_is_pointer_like(
               walk, psx_semantic_node_expression_qual_type(lhs)))
    number = lhs;
  long long value = 0;
  if (!integer_literal_value(walk, number, &value) || value == 0) return;
  diag_warn_tokf_in(
      walk->diagnostics,
      DIAG_WARN_PARSER_POINTER_INTEGER_COMPARE,
      walk->fallback_diag_tok,
      diag_warn_message_for_in(
          walk->diagnostics,
          DIAG_WARN_PARSER_POINTER_INTEGER_COMPARE),
      value, binary_operator_text(node->spec.kind));
}

static void warn_comparison(
    typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *node) {
  const psx_semantic_node_t *lhs =
      child_with_edge(node, PSX_HIR_EDGE_LHS);
  const psx_semantic_node_t *rhs =
      child_with_edge(node, PSX_HIR_EDGE_RHS);
  if ((node->spec.kind == PSX_HIR_EQ ||
       node->spec.kind == PSX_HIR_NE) &&
      lhs && lhs->spec.kind == PSX_HIR_LOGICAL_NOT) {
    const char *op = binary_operator_text(node->spec.kind);
    diag_warn_tokf_in(
        walk->diagnostics,
        DIAG_WARN_PARSER_LOGICAL_NOT_PARENTHESES,
        walk->fallback_diag_tok,
        diag_warn_message_for_in(
            walk->diagnostics,
            DIAG_WARN_PARSER_LOGICAL_NOT_PARENTHESES),
        op, op, op, op);
  }
  warn_self_or_logical_compare(walk, node, lhs, rhs);
  if (node->spec.kind == PSX_HIR_LOGAND ||
      node->spec.kind == PSX_HIR_LOGOR)
    return;
  warn_sign_compare(walk, node, lhs, rhs);
  if (node->spec.kind == PSX_HIR_EQ ||
      node->spec.kind == PSX_HIR_NE)
    warn_pointer_integer_compare(walk, node, lhs, rhs);
  else
    warn_unsigned_zero(walk, node, lhs, rhs);
}

static int plain_int_literal(
    const typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *node, long long *value) {
  const psx_type_t *type = node
      ? canonical_type(walk, psx_semantic_node_expression_qual_type(node))
      : NULL;
  return integer_literal_value(walk, node, value) && type &&
         type->kind == PSX_TYPE_INTEGER &&
         type->integer_kind == PSX_INTEGER_KIND_INT &&
         !ps_type_is_unsigned(type);
}

static void warn_arithmetic(
    typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *node) {
  const psx_semantic_node_t *lhs =
      child_with_edge(node, PSX_HIR_EDGE_LHS);
  const psx_semantic_node_t *rhs =
      child_with_edge(node, PSX_HIR_EDGE_RHS);
  long long lhs_value = 0;
  long long rhs_value = 0;
  if ((node->spec.kind == PSX_HIR_ADD ||
       node->spec.kind == PSX_HIR_SUB ||
       node->spec.kind == PSX_HIR_MUL) &&
      plain_int_literal(walk, lhs, &lhs_value) &&
      plain_int_literal(walk, rhs, &rhs_value)) {
    long long result = node->spec.kind == PSX_HIR_ADD
                           ? lhs_value + rhs_value
                           : node->spec.kind == PSX_HIR_SUB
                                 ? lhs_value - rhs_value
                                 : lhs_value * rhs_value;
    if (result < -2147483648LL || result > 2147483647LL)
      diag_warn_tokf_in(
          walk->diagnostics, DIAG_WARN_PARSER_INTEGER_OVERFLOW,
          walk->fallback_diag_tok,
          diag_warn_message_for_in(
              walk->diagnostics,
              DIAG_WARN_PARSER_INTEGER_OVERFLOW),
          lhs_value, binary_operator_text(node->spec.kind),
          rhs_value, result);
  }
  if ((node->spec.kind == PSX_HIR_SHL ||
       node->spec.kind == PSX_HIR_SHR) &&
      integer_literal_value(walk, rhs, &rhs_value)) {
    psx_qual_type_t lhs_type = lhs
        ? psx_semantic_node_expression_qual_type(lhs)
        : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                            PSX_TYPE_QUALIFIER_NONE};
    int width = canonical_type_size(walk, lhs_type) >= 8
                    ? 64 : 32;
    if (rhs_value < 0 || rhs_value >= width)
      diag_warn_tokf_in(
          walk->diagnostics,
          DIAG_WARN_PARSER_SHIFT_OUT_OF_RANGE,
          walk->fallback_diag_tok,
          diag_warn_message_for_in(
              walk->diagnostics,
              DIAG_WARN_PARSER_SHIFT_OUT_OF_RANGE),
          rhs_value, width,
          binary_operator_text(node->spec.kind));
  }
  if ((node->spec.kind == PSX_HIR_DIV ||
       node->spec.kind == PSX_HIR_MOD) &&
      integer_literal_value(walk, rhs, &rhs_value) && rhs_value == 0)
    diag_warn_tokf_in(
        walk->diagnostics, DIAG_WARN_PARSER_DIVIDE_BY_ZERO,
        walk->fallback_diag_tok,
        diag_warn_message_for_in(
            walk->diagnostics,
            DIAG_WARN_PARSER_DIVIDE_BY_ZERO),
        binary_operator_text(node->spec.kind));
}

static void warn_decl_initializer_overflow(
    typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *node,
    const psx_semantic_node_t *target,
    const psx_semantic_node_t *value) {
  if (!node || !node->spec.is_declaration_initializer ||
      !target || !value || target->spec.kind != PSX_HIR_LOCAL)
    return;
  long long integer_value = 0;
  if (!integer_literal_value(walk, value, &integer_value)) return;
  const psx_type_t *target_type = canonical_type(
      walk, psx_semantic_node_expression_qual_type(target));
  if (!target_type || target_type->kind == PSX_TYPE_BOOL) return;
  int type_size = canonical_type_size(
      walk, psx_semantic_node_expression_qual_type(target));
  if (type_size <= 0 || type_size >= 4) return;
  int bits = type_size * 8;
  long long max_signed = (1LL << (bits - 1)) - 1;
  long long min_signed = -(1LL << (bits - 1));
  long long max_unsigned = (1LL << bits) - 1;
  int out_of_range = ps_type_is_unsigned(target_type)
                         ? integer_value < 0 ||
                               integer_value > max_unsigned
                         : integer_value < min_signed ||
                               integer_value > max_signed;
  if (ps_type_is_unsigned(target_type) && integer_value < 0 &&
      integer_value >= min_signed)
    out_of_range = 0;
  if (out_of_range)
    diag_warn_tokf_in(
        walk->diagnostics, DIAG_WARN_PARSER_CONSTANT_OVERFLOW,
        walk->fallback_diag_tok,
        diag_warn_message_for_in(
            walk->diagnostics,
            DIAG_WARN_PARSER_CONSTANT_OVERFLOW),
        integer_value, type_size);
}

static void warn_return_stack_address(
    typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *node) {
  const psx_semantic_node_t *value =
      child_with_edge(node, PSX_HIR_EDGE_LHS);
  const psx_semantic_node_t *object =
      value && value->spec.kind == PSX_HIR_ADDRESS
          ? child_with_edge(value, PSX_HIR_EDGE_LHS) : NULL;
  if (!object || object->spec.kind != PSX_HIR_LOCAL ||
      !object->spec.name)
    return;
  diag_warn_tokf_in(
      walk->diagnostics,
      DIAG_WARN_PARSER_RETURN_STACK_ADDRESS,
      walk->fallback_diag_tok,
      diag_warn_message_for_in(
          walk->diagnostics,
          DIAG_WARN_PARSER_RETURN_STACK_ADDRESS),
      (int)object->spec.name_length, object->spec.name);
}

static void warn_condition(
    typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *node) {
  const psx_semantic_node_t *condition =
      child_with_edge(node, PSX_HIR_EDGE_LHS);
  const char *context = node->spec.kind == PSX_HIR_IF ? "if" : "while";
  if (condition && condition->spec.kind == PSX_HIR_ASSIGN)
    diag_warn_tokf_in(
        walk->diagnostics,
        DIAG_WARN_PARSER_ASSIGN_IN_CONDITION,
        walk->fallback_diag_tok,
        diag_warn_message_for_in(
            walk->diagnostics,
            DIAG_WARN_PARSER_ASSIGN_IN_CONDITION),
        context);
  else if (condition && condition->spec.kind == PSX_HIR_COMMA)
    diag_warn_tokf_in(
        walk->diagnostics,
        DIAG_WARN_PARSER_COMMA_IN_CONDITION,
        walk->fallback_diag_tok,
        diag_warn_message_for_in(
            walk->diagnostics,
            DIAG_WARN_PARSER_COMMA_IN_CONDITION),
        context);
  const psx_semantic_node_t *body =
      child_with_edge(node, PSX_HIR_EDGE_RHS);
  if (node->spec.kind == PSX_HIR_IF && body &&
      body->spec.kind == PSX_HIR_NOP)
    diag_warn_tokf_in(
        walk->diagnostics, DIAG_WARN_PARSER_EMPTY_BODY,
        walk->fallback_diag_tok, "%s",
        diag_warn_message_for_in(
            walk->diagnostics, DIAG_WARN_PARSER_EMPTY_BODY));
}

static void warn_float_to_int(
    typed_hir_diagnostic_walk_t *walk,
    psx_qual_type_t target_type,
    const psx_semantic_node_t *value) {
  if (!value || !type_is_integer(walk, target_type) ||
      !type_is_floating(
          walk, psx_semantic_node_expression_qual_type(value)) ||
      known_integral_floating_literal(walk, value))
    return;
  char detail[64] = "";
  if (value->spec.kind == PSX_HIR_NUMBER)
    snprintf(detail, sizeof(detail), " (%g)", value->spec.floating_value);
  diag_warn_tokf_in(
      walk->diagnostics, DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING,
      walk->fallback_diag_tok,
      diag_warn_message_for_in(
          walk->diagnostics, DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING),
      detail);
}

static void diagnose_node(
    typed_hir_diagnostic_walk_t *walk,
    const psx_semantic_node_t *node) {
  if (!node) return;
  if (node->spec.kind == PSX_HIR_ASSIGN) {
    const psx_semantic_node_t *target =
        child_with_edge(node, PSX_HIR_EDGE_LHS);
    const psx_semantic_node_t *value =
        child_with_edge(node, PSX_HIR_EDGE_RHS);
    if (target)
      warn_float_to_int(
          walk, psx_semantic_node_expression_qual_type(target), value);
    if (node->spec.is_source_assignment &&
        node_identity_equal(walk, target, value))
      diag_warn_tokf_in(
          walk->diagnostics, DIAG_WARN_PARSER_SELF_ASSIGN,
          walk->fallback_diag_tok, "%s",
          diag_warn_message_for_in(
              walk->diagnostics, DIAG_WARN_PARSER_SELF_ASSIGN));
    warn_decl_initializer_overflow(walk, node, target, value);
  } else if (node->spec.kind == PSX_HIR_RETURN) {
    warn_float_to_int(
        walk, walk->function_return_type,
        child_with_edge(node, PSX_HIR_EDGE_LHS));
    warn_return_stack_address(walk, node);
  } else if (node->spec.kind == PSX_HIR_CALL &&
             node->spec.is_implicit_call && node->spec.name) {
    diag_warn_tokf_in(
        walk->diagnostics, DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL,
        walk->fallback_diag_tok,
        diag_warn_message_for_in(
            walk->diagnostics,
            DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL),
        (int)node->spec.name_length, node->spec.name);
  } else if (node->spec.kind == PSX_HIR_LOGAND ||
             node->spec.kind == PSX_HIR_LOGOR ||
             node->spec.kind == PSX_HIR_EQ ||
             node->spec.kind == PSX_HIR_NE ||
             node->spec.kind == PSX_HIR_LT ||
             node->spec.kind == PSX_HIR_LE ||
             node->spec.kind == PSX_HIR_GT ||
             node->spec.kind == PSX_HIR_GE) {
    warn_comparison(walk, node);
  } else if (node->spec.kind == PSX_HIR_ADD ||
             node->spec.kind == PSX_HIR_SUB ||
             node->spec.kind == PSX_HIR_MUL ||
             node->spec.kind == PSX_HIR_DIV ||
             node->spec.kind == PSX_HIR_MOD ||
             node->spec.kind == PSX_HIR_SHL ||
             node->spec.kind == PSX_HIR_SHR) {
    warn_arithmetic(walk, node);
  } else if (node->spec.kind == PSX_HIR_IF ||
             node->spec.kind == PSX_HIR_WHILE) {
    warn_condition(walk, node);
  }
}

void psx_emit_typed_hir_warnings(
    psx_semantic_context_t *semantic_context,
    const psx_typed_hir_tree_t *tree,
    const token_t *fallback_diag_tok) {
  if (!semantic_context || !tree || !tree->root) return;
  typed_hir_diagnostic_walk_t walk = {
      .semantic_context = semantic_context,
      .diagnostics = ps_ctx_diagnostics(semantic_context),
      .fallback_diag_tok = fallback_diag_tok,
      .function_return_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  if (tree->root->spec.kind == PSX_HIR_FUNCTION) {
    walk.function_return_type = psx_semantic_type_table_base(
        ps_ctx_semantic_type_table_in(semantic_context),
        tree->root->spec.attached_qual_type.type_id);
  }
  size_t count = 1;
  size_t capacity = 64;
  const psx_semantic_node_t **stack = malloc(
      capacity * sizeof(*stack));
  if (!stack) return;
  stack[0] = tree->root;
  while (count > 0) {
    const psx_semantic_node_t *node = stack[--count];
    diagnose_node(&walk, node);
    size_t required = count + node->spec.child_count;
    if (required > capacity) {
      size_t next = capacity;
      while (next < required && next <= SIZE_MAX / 2) next *= 2;
      if (next < required || next > SIZE_MAX / sizeof(*stack)) {
        free(stack);
        return;
      }
      const psx_semantic_node_t **resized = realloc(
          stack, next * sizeof(*stack));
      if (!resized) {
        free(stack);
        return;
      }
      stack = resized;
      capacity = next;
    }
    for (size_t i = node->spec.child_count; i > 0; i--)
      stack[count++] = node->children[i - 1];
  }
  free(stack);
}
