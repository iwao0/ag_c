#include "complex_part_lowering.h"

#include "runtime_context.h"
#include "../parser/node_utils.h"

node_t *lower_complex_part_expression(
    psx_lowering_context_t *lowering_context, node_t *node) {
  if (!node || (node->kind != ND_CREAL && node->kind != ND_CIMAG) ||
      !node->lhs) {
    return node;
  }
  const psx_type_t *operand_type = ps_node_get_type(node->lhs);
  if (!operand_type || operand_type->kind == PSX_TYPE_COMPLEX) return node;
  if (node->kind == ND_CREAL) return node->lhs;
  if (operand_type->kind == PSX_TYPE_FLOAT) return node;
  arena_context_t *arena_context = ps_lowering_arena(lowering_context);
  return ps_node_new_integer_cast_result_in(
      arena_context, ps_node_new_num_in(arena_context, 0),
      ps_node_get_type(node));
}
