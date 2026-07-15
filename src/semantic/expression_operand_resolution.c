#include "expression_operand_resolution.h"

#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type.h"
#include "../parser/type_builder.h"

#include <string.h>

psx_deref_operand_status_t psx_resolve_deref_operand(node_t *operand) {
  if (!operand) return PSX_DEREF_OPERAND_NOT_POINTER;
  const psx_type_t *type = ps_node_get_type(operand);
  if (!type || !ps_type_is_pointer_like(type))
    return PSX_DEREF_OPERAND_NOT_POINTER;
  if (type->base && type->base->kind == PSX_TYPE_VOID)
    return PSX_DEREF_OPERAND_VOID_POINTER;
  return PSX_DEREF_OPERAND_OK;
}

const psx_type_t *psx_resolve_indirection_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand) {
  const psx_type_t *type = ps_node_get_type(operand);
  if (!type || !type->base ||
      (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY))
    return NULL;
  return ps_type_clone_in(ps_ctx_arena(semantic_context), type->base);
}

const psx_type_t *psx_resolve_arithmetic_unary_result_type(
    psx_semantic_context_t *semantic_context,
    node_kind_t kind, node_t *operand) {
  const psx_type_t *type = ps_node_get_type(operand);
  if (!type) return NULL;
  if (kind == ND_UNARY_NEGATE) {
    if (type->kind == PSX_TYPE_BOOL ||
        (type->kind == PSX_TYPE_INTEGER &&
         ps_type_integer_rank(type) < 3))
      return ps_type_new_integer_in(
          ps_ctx_arena(semantic_context), TK_INT, 0);
    if (type->kind == PSX_TYPE_INTEGER || type->kind == PSX_TYPE_FLOAT ||
        type->kind == PSX_TYPE_COMPLEX)
      return ps_type_clone_in(ps_ctx_arena(semantic_context), type);
    return NULL;
  }
  if (kind != ND_CREAL && kind != ND_CIMAG) return NULL;
  if (type->kind == PSX_TYPE_COMPLEX) {
    psx_floating_kind_t floating_kind =
        type->floating_kind != PSX_FLOATING_KIND_NONE
            ? type->floating_kind
            : PSX_FLOATING_KIND_DOUBLE;
    return ps_type_new_floating_in(
        ps_ctx_arena(semantic_context), floating_kind, 0);
  }
  if (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_INTEGER ||
      type->kind == PSX_TYPE_BOOL)
    return ps_type_clone_in(ps_ctx_arena(semantic_context), type);
  return NULL;
}

const psx_type_t *psx_resolve_binary_result_type(
    psx_semantic_context_t *semantic_context,
    node_kind_t kind, node_t *lhs, node_t *rhs) {
  psx_type_binary_op_t op;
  if (!ps_node_binary_type_op(kind, &op)) return NULL;
  return ps_type_binary_result_for_target_in(
      ps_ctx_arena(semantic_context), ps_ctx_target_info(semantic_context), op,
      ps_node_get_type(lhs), ps_node_get_type(rhs));
}

const psx_type_t *psx_resolve_conditional_result_type(
    psx_semantic_context_t *semantic_context,
    node_t *then_expr, node_t *else_expr) {
  return ps_type_conditional_result_for_target_in(
      ps_ctx_arena(semantic_context), ps_ctx_target_info(semantic_context),
      ps_node_get_type(then_expr), ps_node_get_type(else_expr));
}

const psx_type_t *psx_resolve_sequence_result_type(
    psx_semantic_context_t *semantic_context, node_t *value) {
  return ps_type_clone_in(
      ps_ctx_arena(semantic_context), ps_node_get_type(value));
}

const psx_type_t *psx_resolve_address_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand) {
  const psx_type_t *operand_type = ps_node_get_type(operand);
  if (!operand_type) return NULL;
  psx_type_t *base = ps_type_clone_in(
      ps_ctx_arena(semantic_context), operand_type);
  return ps_type_new_pointer_in(ps_ctx_arena(semantic_context), base);
}

const psx_type_t *psx_resolve_incdec_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand) {
  const psx_type_t *type = ps_node_get_type(operand);
  if (!type) return NULL;
  if (ps_type_is_pointer(type) || type->kind == PSX_TYPE_BOOL ||
      type->kind == PSX_TYPE_INTEGER || type->kind == PSX_TYPE_FLOAT)
    return ps_type_clone_in(ps_ctx_arena(semantic_context), type);
  return NULL;
}

static int is_subscript_pointer_operand(node_t *node) {
  const psx_type_t *type = ps_node_get_type(node);
  return type && (type->kind == PSX_TYPE_POINTER ||
                  type->kind == PSX_TYPE_ARRAY);
}

void psx_resolve_subscript_operands(
    node_t *left, node_t *right,
    psx_subscript_operands_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->base = left;
  resolution->index = right;
  int left_is_pointer = is_subscript_pointer_operand(left);
  int right_is_pointer = is_subscript_pointer_operand(right);
  if (!left_is_pointer && !right_is_pointer) {
    resolution->status = PSX_SUBSCRIPT_OPERANDS_INVALID;
    return;
  }
  if (!left_is_pointer && right_is_pointer) {
    resolution->base = right;
    resolution->index = left;
    resolution->swapped = 1;
  }
  resolution->status = PSX_SUBSCRIPT_OPERANDS_OK;
}
