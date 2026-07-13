#include "unary_operator_lowering.h"

#include "../parser/node_utils.h"

node_t *lower_unary_negate_expression(node_t *node) {
  if (!node || node->kind != ND_UNARY_NEGATE || !node->lhs) return node;
  psx_type_t *operand_type = ps_node_get_type(node->lhs);
  if (operand_type && operand_type->kind == PSX_TYPE_FLOAT) {
    node_t *negated = ps_node_new_binary(ND_FNEG, node->lhs, NULL);
    ps_node_bind_type(negated, node->type);
    return negated;
  }
  node_t *negated = ps_node_new_binary(
      ND_SUB, ps_node_new_num(0), node->lhs);
  ps_node_bind_type(negated, node->type);
  return negated;
}

node_t *lower_complex_part_expression(node_t *node) {
  if (!node || (node->kind != ND_CREAL && node->kind != ND_CIMAG) ||
      !node->lhs) {
    return node;
  }
  psx_type_t *operand_type = ps_node_get_type(node->lhs);
  if (!operand_type || operand_type->kind == PSX_TYPE_COMPLEX) return node;
  if (node->kind == ND_CREAL) return node->lhs;
  if (operand_type->kind == PSX_TYPE_FLOAT) return node;
  return ps_node_new_integer_cast_result(
      ps_node_new_num(0), node->type);
}
