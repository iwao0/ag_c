#include "expr_lowering.h"
#include "../parser/node_type_public.h"
#include "../parser/node_utils.h"
#include "../parser/node_vla_public.h"


static int is_pointer_arithmetic_operand(node_t *node) {
  if (ps_node_value_is_pointer_like(node)) return 1;

  if ((node->kind == ND_DEREF || node->kind == ND_ADDR) &&
      !ps_node_value_is_pointer_like(node)) {
    int deref_size = ps_node_deref_size(node);
    if (deref_size > 0 && ps_node_type_size(node) > deref_size) return 1;
  }
  return 0;
}

static node_t *scale_pointer_offset(node_t *pointer, node_t *offset) {
  int stride_offset = ps_node_vla_row_stride_frame_off(pointer);
  if (stride_offset != 0) {
    return ps_node_new_binary(
        ND_MUL, offset, ps_node_new_lvar_typed(stride_offset, 8));
  }

  int deref_size = ps_node_deref_size(pointer);
  if (deref_size > 1) {
    return ps_node_new_binary(ND_MUL, offset, ps_node_new_num(deref_size));
  }
  return offset;
}

static node_t *new_pointer_result(node_kind_t kind, node_t *pointer,
                                  node_t *offset) {
  node_t *result = ps_node_new_binary(kind, pointer, offset);
  psx_type_t *type = ps_node_row_decay_pointer_arith_type(pointer);
  if (type) ps_node_bind_type(result, type);
  return result;
}

node_t *lower_additive_expression(node_kind_t kind, node_t *lhs, node_t *rhs) {
  if (kind == ND_ADD) {
    if (!is_pointer_arithmetic_operand(lhs) && is_pointer_arithmetic_operand(rhs)) {
      node_t *tmp = lhs;
      lhs = rhs;
      rhs = tmp;
    }
    if (is_pointer_arithmetic_operand(lhs)) {
      return new_pointer_result(ND_ADD, lhs, scale_pointer_offset(lhs, rhs));
    }
  } else if (kind == ND_SUB && is_pointer_arithmetic_operand(lhs)) {
    if (is_pointer_arithmetic_operand(rhs)) {
      node_t *difference = ps_node_new_binary(ND_SUB, lhs, rhs);
      int deref_size = ps_node_deref_size(lhs);
      return deref_size > 1
                 ? ps_node_new_binary(ND_DIV, difference,
                                       ps_node_new_num(deref_size))
                 : difference;
    }
    return new_pointer_result(ND_SUB, lhs, scale_pointer_offset(lhs, rhs));
  }

  node_t *result = ps_node_new_binary(kind, lhs, rhs);
  result->source_op = kind == ND_ADD ? TK_PLUS : TK_MINUS;
  return result;
}
