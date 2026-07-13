#include "subscript_lowering.h"

#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type.h"

static node_t *make_scaled_offset(node_t *base, node_t *index,
                                  int *out_elem_size, int *out_inner_stride,
                                  int *out_next_stride, int *out_extra_strides,
                                  int *out_extra_count) {
  int deref_size = ps_node_deref_size(base);
  int type_size = ps_node_type_size(base);
  int elem_size = deref_size ? deref_size : (type_size ? type_size : 8);
  if (base->kind == ND_DEREF &&
      ps_node_pointer_qual_levels(base) == 1 &&
      ps_node_base_deref_size(base) > 0) {
    token_kind_t tag_kind = TK_EOF;
    ps_node_get_tag_type(base, &tag_kind, NULL, NULL, NULL);
    if (deref_size == 0 &&
        !(ps_node_is_pointer(base) &&
          !ps_node_scalar_ptr_member_lvalue(base) && base->lhs &&
          base->lhs->kind == ND_ADD &&
          ps_ctx_is_tag_aggregate_kind(tag_kind))) {
      elem_size = ps_node_base_deref_size(base);
    }
  }

  int runtime_stride_slot = ps_node_vla_row_stride_frame_off(base);
  int inner_stride = 0;
  int next_stride = 0;
  int extra_strides[5] = {0};
  int extra_count = 0;
  ps_node_pointer_stride_metadata(base, &inner_stride, &next_stride,
                                  extra_strides, &extra_count);
  psx_type_t *base_type = ps_node_get_type(base);
  int canonical_stride = 0;
  if (base_type && base_type->kind == PSX_TYPE_POINTER &&
      base_type->base && base_type->base->kind == PSX_TYPE_POINTER &&
      base_type->base->base && base_type->base->base->kind == PSX_TYPE_ARRAY) {
    elem_size = 8;
    inner_stride = 0;
    next_stride = 0;
    extra_count = 0;
    canonical_stride = 1;
  }
  if (base_type && base_type->kind == PSX_TYPE_POINTER &&
      base_type->base && base_type->base->kind == PSX_TYPE_ARRAY) {
    int array_size = ps_type_sizeof(base_type->base);
    if (array_size > 0) elem_size = array_size;
    canonical_stride = 1;
  }
  if (base_type && base_type->kind == PSX_TYPE_ARRAY &&
      base_type->base && base_type->base->kind == PSX_TYPE_POINTER) {
    int pointer_elem_size = ps_type_sizeof(base_type->base);
    if (pointer_elem_size > 0) elem_size = pointer_elem_size;
    inner_stride = 0;
    next_stride = 0;
    extra_count = 0;
    canonical_stride = 1;
  }

  int ptr_array_bytes = ps_node_ptr_array_pointee_bytes(base);
  if (!canonical_stride && ptr_array_bytes > 0 && base->kind == ND_DEREF &&
      base_type && base_type->kind == PSX_TYPE_ARRAY && base_type->base &&
      ps_type_is_pointer(base_type->base)) {
    int pointer_elem_size = ps_type_sizeof(base_type->base);
    if (pointer_elem_size > 0) elem_size = pointer_elem_size;
  }
  if (!canonical_stride && ptr_array_bytes > 0 && base->kind != ND_DEREF) {
    int base_deref_size = ps_node_base_deref_size(base);
    int has_outer_row_stride = deref_size > 8 && inner_stride > 0;
    if (!has_outer_row_stride) {
      elem_size = deref_size == 8 && ptr_array_bytes > deref_size
                      ? deref_size
                      : ptr_array_bytes;
      if (base_deref_size > 0 && inner_stride == ptr_array_bytes)
        inner_stride = base_deref_size;
    }
  }
  if (!canonical_stride && base->kind == ND_DEREF &&
      ps_node_pointer_qual_levels(base) == 0 && inner_stride <= 0 &&
      runtime_stride_slot == 0) {
    int base_deref_size = ps_node_base_deref_size(base);
    if (base_deref_size == 0 && base->lhs)
      base_deref_size = ps_node_base_deref_size(base->lhs);
    int pointer_array_element_row =
        ptr_array_bytes > 0 && deref_size > base_deref_size;
    if (base_deref_size > 0 && type_size > base_deref_size &&
        !pointer_array_element_row) {
      elem_size = base_deref_size;
    }
  }

  node_t *scaled;
  if (runtime_stride_slot) {
    node_t *stride = ps_node_new_lvar_typed(runtime_stride_slot, 8);
    scaled = ps_node_new_binary(ND_MUL, index, stride);
    elem_size = inner_stride ? inner_stride : 1;
  } else {
    scaled = ps_node_new_binary(ND_MUL, index, ps_node_new_num(elem_size));
  }
  *out_elem_size = elem_size;
  *out_inner_stride = inner_stride;
  *out_next_stride = next_stride;
  *out_extra_count = extra_count;
  for (int i = 0; i < extra_count && i < 5; i++)
    out_extra_strides[i] = extra_strides[i];
  return scaled;
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
  int elem_size = 0;
  int inner_stride = 0;
  int next_stride = 0;
  int extra_strides[5] = {0};
  int extra_count = 0;
  node_t *scaled = make_scaled_offset(
      base, index, &elem_size, &inner_stride, &next_stride,
      extra_strides, &extra_count);
  node_t *lowered = ps_node_new_subscript_deref_for(
      base, base_address_of(base), scaled, elem_size, inner_stride,
      next_stride, extra_strides, extra_count);
  *node = *lowered;
  node->tok = source_tok;
}
