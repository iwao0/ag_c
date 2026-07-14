#include "vla_lowering.h"

#include "frame_layout.h"
#include "local_storage.h"
#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/local_registry.h"
#include "../parser/node_utils.h"
#include "../parser/type.h"
#include "../diag/diag.h"
#include <stdlib.h>
#include <string.h>

static node_t *append_init(node_t *chain, node_t *node) {
  return chain ? ps_node_new_binary(ND_COMMA, chain, node) : node;
}

static lvar_t *create_vla_storage(
    char *name, int name_len, int storage_size, int alignment,
    const psx_type_t *type) {
  if (!type) return NULL;
  int offset = local_storage_allocate(storage_size, alignment);
  return ps_local_registry_create_storage_object(
      name, name_len, offset, storage_size, alignment, type);
}

static psx_type_t *runtime_stride_storage_type(int count) {
  if (count <= 0) return NULL;
  psx_type_t *slot = ps_type_new_integer(TK_LONG, 8, 0);
  return count == 1
             ? slot
             : ps_type_new_array(slot, count, count * 8, 0);
}

psx_vla_lowering_result_t lower_vla_declaration(
    const psx_vla_lowering_request_t *request) {
  psx_vla_lowering_result_t result = {0};
  int count = request ? request->dimension_count : 0;
  int element_size =
      request ? ps_type_pointee_value_size(request->type) : 0;
  if (!request || !request->type || count <= 0 ||
      element_size <= 0 || !request->dimensions || !request->const_values ||
      !request->is_const) {
    ps_diag_ctx(request ? request->diag_tok : NULL, "vla-lowering", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  }

  frame_vla_layout_t layout = frame_layout_vla_storage(
      count, count == 2 && request->is_const[1]);
  int row_stride_offset = 0;
  int outer_stride = 0;
  int remaining_strides = 0;
  if (count == 1) {
    outer_stride = element_size;
  } else if (count == 2 && request->is_const[1]) {
    outer_stride = (int)request->const_values[1] * element_size;
  }

  result.var = create_vla_storage(
      request->name, request->name_len, layout.storage_size,
      request->requested_alignment, request->type);
  if (!result.var) return result;
  int var_offset = ps_lvar_offset(result.var);
  if (layout.row_stride_relative_offset > 0)
    row_stride_offset = var_offset + layout.row_stride_relative_offset;
  if (count >= 3) remaining_strides = layout.subsequent_stride_count;
  ps_local_registry_set_vla_descriptor(
      result.var, row_stride_offset, remaining_strides, 0, 0);

  node_t *alloc_lhs = NULL;
  node_t *alloc_rhs = NULL;
  if (count == 1) {
    alloc_lhs = ps_node_new_binary(
        ND_MUL, request->dimensions[0],
        ps_node_new_num(element_size));
  } else if (count == 2 && request->is_const[1]) {
    alloc_lhs = ps_node_new_binary(
        ND_MUL, request->dimensions[0], ps_node_new_num(outer_stride));
  } else if (count == 2) {
    alloc_lhs = request->dimensions[0];
    alloc_rhs = ps_node_new_binary(
        ND_MUL, request->dimensions[1],
        ps_node_new_num(element_size));
  } else {
    node_t *outer = ps_node_new_num(element_size);
    for (int i = count - 1; i >= 1; i--)
      outer = ps_node_new_binary(ND_MUL, outer, request->dimensions[i]);
    alloc_lhs = request->dimensions[0];
    alloc_rhs = outer;
  }
  result.init = ps_node_new_vla_alloc(
      var_offset, row_stride_offset, alloc_lhs, alloc_rhs);

  for (int level = 1; level < count - 1; level++) {
    node_t *stride = ps_node_new_num(element_size);
    for (int i = count - 1; i >= level + 1; i--)
      stride = ps_node_new_binary(ND_MUL, stride, request->dimensions[i]);
    node_t *slot = ps_node_new_lvar_typed(
        frame_layout_vla_stride_offset(var_offset, level), 8);
    result.init = append_init(
        result.init, ps_node_new_assign(slot, stride));
  }
  return result;
}

psx_vla_lowering_result_t lower_pointer_to_vla_declaration(
    const psx_pointer_vla_lowering_request_t *request) {
  psx_vla_lowering_result_t result = {0};
  int element_size =
      request ? ps_type_pointee_value_size(request->type) : 0;
  if (!request || !request->type || !request->name || request->name_len <= 0 ||
      element_size <= 0 || !request->row_dimension) {
    ps_diag_ctx(request ? request->diag_tok : NULL, "vla-lowering", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  }

  frame_vla_layout_t layout = frame_layout_pointer_vla_storage();
  result.var = create_vla_storage(
      request->name, request->name_len, layout.storage_size,
      request->requested_alignment, request->type);
  if (!result.var) return result;
  int row_stride_offset =
      ps_lvar_offset(result.var) + layout.row_stride_relative_offset;
  ps_local_registry_set_vla_descriptor(
      result.var, row_stride_offset, 0, 0, element_size);

  node_t *slot = ps_node_new_lvar_typed(row_stride_offset, 8);
  node_t *stride = ps_node_new_binary(
      ND_MUL, request->row_dimension,
      ps_node_new_num(element_size));
  result.init = (node_t *)ps_node_new_assign(slot, stride);
  return result;
}

static char *parameter_stride_storage_name(
    const char *name, int name_len, int *out_len) {
  static const char prefix[] = "__rs_";
  int prefix_len = (int)sizeof(prefix) - 1;
  int length = prefix_len + name_len;
  char *result = malloc((size_t)length + 1);
  if (!result) return NULL;
  memcpy(result, prefix, (size_t)prefix_len);
  memcpy(result + prefix_len, name, (size_t)name_len);
  result[length] = '\0';
  if (out_len) *out_len = length;
  return result;
}

psx_parameter_vla_lowering_result_t lower_parameter_vla_declaration(
    const psx_parameter_vla_lowering_request_t *request) {
  psx_parameter_vla_lowering_result_t result = {0};
  int count = request ? request->inner_dimension_count : 0;
  int element_size =
      request ? ps_type_pointee_value_size(request->type) : 0;
  if (!request || !request->type || !request->name || request->name_len <= 0 ||
      element_size <= 0 || count < 0 ||
      (count > 0 && !request->inner_dimensions)) {
    ps_diag_ctx(request ? request->diag_tok : NULL, "vla-lowering", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  }

  result.var = create_vla_storage(
      request->name, request->name_len, 8, 0, request->type);
  if (!result.var) return result;

  int has_runtime_dimension = 0;
  for (int i = 0; i < count; i++) {
    if (request->inner_dimensions[i].constant <= 0) {
      has_runtime_dimension = 1;
      break;
    }
  }

  if (has_runtime_dimension) {
    int stride_name_len = 0;
    char *stride_name = parameter_stride_storage_name(
        request->name, request->name_len, &stride_name_len);
    result.stride_storage = create_vla_storage(
        stride_name, stride_name_len, 8 * count, 0,
        runtime_stride_storage_type(count));
    if (!result.stride_storage) return result;

    int *constants = calloc((size_t)count, sizeof(*constants));
    int *source_offsets = calloc((size_t)count, sizeof(*source_offsets));
    if (!constants || !source_offsets) {
      free(constants);
      free(source_offsets);
      ps_diag_ctx(request->diag_tok, "param",
                  "VLA parameter dimension allocation failed");
    }
    for (int i = 0; i < count; i++) {
      const psx_parameter_vla_dimension_t *dimension =
          &request->inner_dimensions[i];
      constants[i] = dimension->constant;
      if (dimension->constant > 0 || !dimension->source_name) continue;
      lvar_t *source = ps_decl_find_lvar(
          dimension->source_name, dimension->source_name_len);
      if (!source || !ps_lvar_is_param(source)) {
        ps_diag_ctx(
            request->diag_tok, "param",
            diag_message_for(
                DIAG_ERR_PARSER_VLA_PARAM_DIM_NOT_PRECEDING_PARAM),
            dimension->source_name_len, dimension->source_name);
      }
      source_offsets[i] = ps_lvar_offset(source);
    }

    ps_local_registry_set_vla_descriptor(
        result.var, ps_lvar_offset(result.stride_storage), count - 1,
        0, element_size);
    ps_local_registry_set_vla_param_inner_dims(
        result.var, constants, source_offsets, count);
    if (count == 1 && constants[0] == 0) {
      ps_local_registry_set_vla_descriptor(
          result.var, ps_lvar_offset(result.stride_storage), 0,
          source_offsets[0], element_size);
    }
    free(constants);
    free(source_offsets);
  }

  /* Keep the current name ineligible while resolving preceding parameters. */
  ps_local_registry_mark_parameter(result.var, 0);
  return result;
}
