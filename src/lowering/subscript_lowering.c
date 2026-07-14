#include "subscript_lowering.h"

#include "../parser/node_utils.h"
#include "../parser/type.h"

static node_t *make_scaled_offset(node_t *base, node_t *index) {
  int runtime_stride_slot = ps_node_vla_row_stride_frame_off(base);
  if (runtime_stride_slot) {
    node_t *stride = ps_node_new_lvar_typed(runtime_stride_slot, 8);
    return ps_node_new_binary(ND_MUL, index, stride);
  }
  int stride = ps_type_subscript_static_stride(ps_node_get_type(base));
  if (stride <= 0) stride = 8;
  return ps_node_new_binary(ND_MUL, index, ps_node_new_num(stride));
}

static node_t *base_address_of(node_t *base) {
  if (base->kind != ND_DEREF) return base;
  if (ps_node_subscript_deref_uses_base_address(base)) return base->lhs;
  if (ps_node_scalar_ptr_member_lvalue(base)) return base;
  if (base->lhs && base->lhs->kind == ND_ADD && base->lhs->rhs &&
      base->lhs->rhs->kind == ND_NUM) {
    return base->lhs;
  }
  return base;
}

void lower_subscript_expression(node_t *node) {
  if (!node || node->kind != ND_SUBSCRIPT) return;
  node_t *base = node->lhs;
  node_t *index = node->rhs;
  token_t *source_tok = node->tok;
  node_t *scaled = make_scaled_offset(base, index);
  node_t *lowered = ps_node_new_subscript_deref_for(
      base, base_address_of(base), scaled);
  *node = *lowered;
  node->tok = source_tok;
}
