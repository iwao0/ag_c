#include "unary_deref_lowering.h"

#include "../parser/node_utils.h"

void lower_unary_deref_expression(node_t *node) {
  if (!node || node->kind != ND_UNARY_DEREF) return;
  token_t *source_tok = node->tok;
  node_t *lowered = ps_node_new_unary_deref_for(node->lhs);
  *node = *lowered;
  node->tok = source_tok;
}
