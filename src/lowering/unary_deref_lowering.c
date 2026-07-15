#include "unary_deref_lowering.h"
#include "runtime_context.h"

#include "../parser/node_utils.h"

node_t *lower_unary_deref_expression(
    psx_lowering_context_t *lowering_context, node_t *node) {
  if (!node || node->kind != ND_UNARY_DEREF) return node;
  token_t *source_tok = node->tok;
  node_t *lowered = ps_node_new_unary_deref_for_in(
      ps_lowering_arena(lowering_context), node->lhs);
  if (!lowered) return node;
  if (!lowered->tok) lowered->tok = source_tok;
  return lowered;
}
