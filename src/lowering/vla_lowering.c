#include "vla_lowering.h"

#include "frame_layout.h"
#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/node_utils.h"
#include "../diag/diag.h"

static node_t *append_init(node_t *chain, node_t *node) {
  return chain ? psx_node_new_binary(ND_COMMA, chain, node) : node;
}

psx_vla_lowering_result_t lower_vla_declaration(
    const psx_vla_lowering_request_t *request) {
  psx_vla_lowering_result_t result = {0};
  int count = request ? request->dimension_count : 0;
  if (!request || count <= 0 || count > PSX_VLA_MAX_DIMS) {
    psx_diag_ctx(request ? request->diag_tok : NULL, "vla-lowering", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  }

  frame_vla_layout_t layout = frame_layout_vla_storage(
      count, count == 2 && request->is_const[1]);
  int row_stride_offset = 0;
  int outer_stride = 0;
  int remaining_strides = 0;
  if (count == 1) {
    outer_stride = request->element_size;
  } else if (count == 2 && request->is_const[1]) {
    outer_stride = (int)request->const_values[1] * request->element_size;
  }

  result.var = psx_decl_register_lvar_sized_align(
      request->name, request->name_len, layout.storage_size,
      request->element_size, 1, 0);
  if (layout.row_stride_relative_offset > 0)
    row_stride_offset = result.var->offset + layout.row_stride_relative_offset;
  if (count >= 3) remaining_strides = layout.subsequent_stride_count;
  psx_decl_set_lvar_vla_descriptor(
      result.var, outer_stride, row_stride_offset,
      remaining_strides, 0, 0);

  node_t *alloc_lhs = NULL;
  node_t *alloc_rhs = NULL;
  if (count == 1) {
    alloc_lhs = psx_node_new_binary(
        ND_MUL, request->dimensions[0],
        psx_node_new_num(request->element_size));
  } else if (count == 2 && request->is_const[1]) {
    alloc_lhs = psx_node_new_binary(
        ND_MUL, request->dimensions[0], psx_node_new_num(outer_stride));
  } else if (count == 2) {
    alloc_lhs = request->dimensions[0];
    alloc_rhs = psx_node_new_binary(
        ND_MUL, request->dimensions[1],
        psx_node_new_num(request->element_size));
  } else {
    node_t *outer = psx_node_new_num(request->element_size);
    for (int i = count - 1; i >= 1; i--)
      outer = psx_node_new_binary(ND_MUL, outer, request->dimensions[i]);
    alloc_lhs = request->dimensions[0];
    alloc_rhs = outer;
  }
  result.init = psx_node_new_vla_alloc(
      result.var->offset, row_stride_offset, alloc_lhs, alloc_rhs);

  for (int level = 1; level < count - 1; level++) {
    node_t *stride = psx_node_new_num(request->element_size);
    for (int i = count - 1; i >= level + 1; i--)
      stride = psx_node_new_binary(ND_MUL, stride, request->dimensions[i]);
    node_t *slot = psx_node_new_lvar_typed(
        frame_layout_vla_stride_offset(result.var->offset, level), 8);
    result.init = append_init(
        result.init, psx_node_new_assign(slot, stride));
  }
  return result;
}
