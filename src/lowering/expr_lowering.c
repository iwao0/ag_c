#include "expr_lowering.h"
#include "runtime_context.h"
#include "../parser/node_type_public.h"
#include "../parser/node_utils.h"
#include "../parser/node_vla_public.h"

static int is_pointer_arithmetic_operand(
    const psx_lowering_context_t *lowering_context, node_t *node) {
  if (ps_node_value_is_pointer_like(node)) return 1;

  if ((node->kind == ND_DEREF || node->kind == ND_ADDR) &&
      !ps_node_value_is_pointer_like(node)) {
    const psx_type_t *type = ps_node_get_type(node);
    int deref_size = ps_lowering_type_deref_size(lowering_context, type);
    if (deref_size > 0 &&
        ps_lowering_type_size(lowering_context, type) > deref_size) {
      return 1;
    }
  }
  return 0;
}

static int pointer_arithmetic_stride(
    const psx_lowering_context_t *lowering_context,
    node_t *pointer) {
  const psx_type_t *type = ps_node_get_type(pointer);
  if (type &&
      (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY) &&
      type->base) {
    int stride = ps_lowering_type_deref_size(lowering_context, type);
    if (stride > 0) return stride;
  }
  return 0;
}

static node_t *scale_pointer_offset(
    psx_lowering_context_t *lowering_context,
    node_t *pointer, node_t *offset) {
  arena_context_t *arena_context = ps_lowering_arena(lowering_context);
  int stride_offset = ps_node_vla_row_stride_frame_off(pointer);
  if (stride_offset != 0) {
    return ps_node_new_binary_in(
        arena_context, ND_MUL, offset,
        ps_node_new_lvar_typed_in(arena_context, stride_offset, 8));
  }

  int deref_size = pointer_arithmetic_stride(lowering_context, pointer);
  if (deref_size > 1) {
    return ps_node_new_binary_in(
        arena_context, ND_MUL, offset,
        ps_node_new_num_in(arena_context, deref_size));
  }
  return offset;
}

static node_t *new_pointer_result(
    psx_lowering_context_t *lowering_context,
    node_kind_t kind, node_t *pointer, node_t *offset) {
  arena_context_t *arena_context = ps_lowering_arena(lowering_context);
  node_t *result = ps_node_new_binary_in(
      arena_context, kind, pointer, offset);
  const psx_type_t *type = ps_node_row_decay_pointer_arith_type_in(
      arena_context, pointer);
  if (type) ps_node_bind_type(result, type);
  return result;
}

node_t *lower_additive_expression(
    psx_lowering_context_t *lowering_context,
    node_kind_t kind, node_t *lhs, node_t *rhs) {
  arena_context_t *arena_context = ps_lowering_arena(lowering_context);
  if (kind == ND_ADD) {
    if (!is_pointer_arithmetic_operand(lowering_context, lhs) &&
        is_pointer_arithmetic_operand(lowering_context, rhs)) {
      node_t *tmp = lhs;
      lhs = rhs;
      rhs = tmp;
    }
    if (is_pointer_arithmetic_operand(lowering_context, lhs)) {
      return new_pointer_result(
          lowering_context, ND_ADD, lhs,
          scale_pointer_offset(lowering_context, lhs, rhs));
    }
  } else if (kind == ND_SUB &&
             is_pointer_arithmetic_operand(lowering_context, lhs)) {
    if (is_pointer_arithmetic_operand(lowering_context, rhs)) {
      node_t *difference = ps_node_new_binary_in(
          arena_context, ND_SUB, lhs, rhs);
      int deref_size = pointer_arithmetic_stride(lowering_context, lhs);
      return deref_size > 1
                 ? ps_node_new_binary_in(
                       arena_context, ND_DIV, difference,
                       ps_node_new_num_in(arena_context, deref_size))
                 : difference;
    }
    return new_pointer_result(
        lowering_context, ND_SUB, lhs,
        scale_pointer_offset(lowering_context, lhs, rhs));
  }

  node_t *result = ps_node_new_binary_in(
      arena_context, kind, lhs, rhs);
  result->source_op = kind == ND_ADD ? TK_PLUS : TK_MINUS;
  return result;
}
