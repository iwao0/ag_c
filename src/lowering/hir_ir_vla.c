#include "hir_ir_builder_internal.h"

#include <stdlib.h>

static int emit_vla_stride_value(
    hir_ir_context_t *context, ir_val_t dimension,
    ir_val_t accumulated, int destination_offset, int slot_size) {
  int product_vreg = hir_ir_new_vreg(context);
  if (product_vreg < 0) return -1;
  ir_inst_t *multiply = ir_inst_new(IR_MUL);
  if (!multiply) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return -1;
  }
  multiply->dst = ir_val_vreg(product_vreg, IR_TY_I32);
  multiply->src1 = dimension;
  multiply->src2 = accumulated;
  if (!hir_ir_append_instruction(context, multiply)) return -1;

  int wide_vreg = hir_ir_new_vreg(context);
  if (wide_vreg < 0) return -1;
  ir_inst_t *extend = ir_inst_new(IR_ZEXT);
  if (!extend) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return -1;
  }
  extend->dst = ir_val_vreg(wide_vreg, IR_TY_I64);
  extend->src1 = multiply->dst;
  if (!hir_ir_append_instruction(context, extend)) return -1;

  int slot = hir_ir_local_storage_address(
      context, destination_offset, slot_size,
      slot_size >= 8 ? 8 : slot_size);
  if (slot < 0) return -1;
  ir_inst_t *store = ir_inst_new(IR_STORE);
  if (!store) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return -1;
  }
  store->src1 = ir_val_vreg(slot, IR_TY_PTR);
  store->src2 = extend->dst;
  if (!hir_ir_append_instruction(context, store)) return -1;
  return product_vreg;
}

static int load_vla_dimension(
    hir_ir_context_t *context, const psx_hir_node_t *parameter,
    size_t dimension, ir_val_t *result) {
  int constant = psx_hir_node_vla_dimension_constant(
      parameter, dimension);
  if (constant > 0) {
    *result = ir_val_imm(IR_TY_I32, constant);
    return 1;
  }
  int source_offset = psx_hir_node_vla_dimension_source_offset(
      parameter, dimension);
  int source = hir_ir_find_local_address(context, source_offset);
  if (source < 0) {
    context->status = IR_HIR_BUILD_INVALID;
    return 0;
  }
  int value_vreg = hir_ir_new_vreg(context);
  if (value_vreg < 0) return 0;
  ir_inst_t *load = ir_inst_new(IR_LOAD);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  load->dst = ir_val_vreg(value_vreg, IR_TY_I32);
  load->src1 = ir_val_vreg(source, IR_TY_PTR);
  if (!hir_ir_append_instruction(context, load)) return 0;
  *result = load->dst;
  return 1;
}

int hir_ir_emit_vla_parameter_strides(
    hir_ir_context_t *context, const psx_hir_node_t *root) {
  size_t parameter_count = hir_ir_child_count_for_edge(
      root, PSX_HIR_EDGE_PARAMETER);
  for (size_t parameter_index = 0;
       parameter_index < parameter_count; parameter_index++) {
    const psx_hir_node_t *parameter = hir_ir_child_for_edge(
        context, root, PSX_HIR_EDGE_PARAMETER, parameter_index);
    int frame_offset = psx_hir_node_vla_stride_frame_offset(parameter);
    int element_size = psx_hir_node_vla_stride_element_size(parameter);
    int slot_size = psx_hir_node_vla_stride_slot_size(parameter);
    size_t dimension_count =
        psx_hir_node_vla_dimension_count(parameter);
    if (frame_offset == 0) continue;
    if (element_size <= 0 || slot_size < 8) {
      context->status = IR_HIR_BUILD_INVALID;
      return 0;
    }
    if (dimension_count == 0) {
      int source = hir_ir_find_local_address(
          context,
          psx_hir_node_vla_stride_source_offset(parameter));
      if (source < 0) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      int value_vreg = hir_ir_new_vreg(context);
      if (value_vreg < 0) return 0;
      ir_inst_t *load = ir_inst_new(IR_LOAD);
      if (!load) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return 0;
      }
      load->dst = ir_val_vreg(value_vreg, IR_TY_I32);
      load->src1 = ir_val_vreg(source, IR_TY_PTR);
      if (!hir_ir_append_instruction(context, load) ||
          emit_vla_stride_value(
              context, load->dst,
              ir_val_imm(IR_TY_I32, element_size),
              frame_offset, slot_size) < 0)
        return 0;
      continue;
    }

    int accumulated_vreg = -1;
    for (size_t reverse = dimension_count; reverse > 0; reverse--) {
      size_t dimension = reverse - 1;
      ir_val_t dimension_value;
      if (!load_vla_dimension(
              context, parameter, dimension, &dimension_value))
        return 0;
      ir_val_t accumulated = dimension == dimension_count - 1
                                 ? ir_val_imm(IR_TY_I32, element_size)
                                 : ir_val_vreg(
                                       accumulated_vreg, IR_TY_I32);
      accumulated_vreg = emit_vla_stride_value(
          context, dimension_value, accumulated,
          frame_offset + slot_size * (int)dimension, slot_size);
      if (accumulated_vreg < 0) return 0;
    }
  }
  return 1;
}

static int store_vla_runtime_value(
    hir_ir_context_t *context, int frame_offset, int slot_size,
    ir_val_t value) {
  ir_val_t wide = hir_ir_emit_integer_width_conversion(
      context, value, IR_TY_I64, 0);
  int slot = hir_ir_local_storage_address(
      context, frame_offset, slot_size, 8);
  return context->status == IR_HIR_BUILD_OK && slot >= 0 &&
         hir_ir_store_direct_value(
             context, ir_val_vreg(slot, IR_TY_PTR), wide);
}

ir_val_t hir_ir_build_vla_allocation(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  size_t dimension_count = hir_ir_child_count_for_edge(
      node, PSX_HIR_EDGE_VLA_DIMENSION);
  size_t store_count =
      psx_hir_node_vla_runtime_store_count(node);
  int descriptor_offset = psx_hir_node_storage_offset(node);
  int element_size =
      psx_hir_node_vla_stride_element_size(node);
  int slot_size = psx_hir_node_vla_stride_slot_size(node);
  if (dimension_count == 0 || element_size <= 0 ||
      slot_size < 8 || (descriptor_offset <= 0 && store_count == 0))
    return hir_ir_unsupported_expr(context);

  ir_val_t *suffix_sizes = calloc(
      dimension_count, sizeof(*suffix_sizes));
  if (!suffix_sizes) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  for (size_t i = 0; i < dimension_count; i++) {
    const psx_hir_node_t *dimension = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_VLA_DIMENSION, i);
    suffix_sizes[i] = hir_ir_build_expr(context, dimension);
    if (!dimension || context->status != IR_HIR_BUILD_OK ||
        !hir_ir_is_integer_type(suffix_sizes[i].type)) {
      free(suffix_sizes);
      return hir_ir_unsupported_expr(context);
    }
    suffix_sizes[i] = hir_ir_emit_integer_width_conversion(
        context, suffix_sizes[i], IR_TY_I32, 0);
    if (context->status != IR_HIR_BUILD_OK) {
      free(suffix_sizes);
      return ir_val_none();
    }
  }

  ir_val_t suffix = ir_val_imm(IR_TY_I32, element_size);
  for (size_t reverse = dimension_count; reverse > 0; reverse--) {
    size_t i = reverse - 1;
    suffix = hir_ir_emit_integer_binary(
        context, IR_MUL, suffix_sizes[i], suffix, IR_TY_I32);
    if (context->status != IR_HIR_BUILD_OK) {
      free(suffix_sizes);
      return ir_val_none();
    }
    suffix_sizes[i] = suffix;
  }

  for (size_t i = 0; i < store_count; i++) {
    int frame_offset =
        psx_hir_node_vla_runtime_store_offset(node, i);
    int start_dimension =
        psx_hir_node_vla_runtime_store_dimension(node, i);
    if (frame_offset <= 0 || start_dimension < 0 ||
        (size_t)start_dimension >= dimension_count ||
        !store_vla_runtime_value(
            context, frame_offset, slot_size,
            suffix_sizes[start_dimension])) {
      free(suffix_sizes);
      return hir_ir_unsupported_expr(context);
    }
  }

  if (descriptor_offset <= 0) {
    free(suffix_sizes);
    return ir_val_imm(IR_TY_I32, 0);
  }

  ir_val_t total_size = suffix_sizes[0];
  if (!store_vla_runtime_value(
          context, descriptor_offset + slot_size,
          slot_size, total_size)) {
    free(suffix_sizes);
    return ir_val_none();
  }

  int base_vreg = hir_ir_new_vreg(context);
  if (base_vreg < 0) {
    free(suffix_sizes);
    return ir_val_none();
  }
  ir_inst_t *allocation = ir_inst_new(IR_VLA_ALLOC);
  if (!allocation) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    free(suffix_sizes);
    return ir_val_none();
  }
  allocation->dst = ir_val_vreg(base_vreg, IR_TY_PTR);
  allocation->src1 = total_size;
  if (!hir_ir_append_instruction(context, allocation)) {
    free(suffix_sizes);
    return ir_val_none();
  }

  int base_slot = hir_ir_local_storage_address(
      context, descriptor_offset, slot_size, 8);
  if (base_slot < 0 ||
      !hir_ir_store_direct_value(
          context, ir_val_vreg(base_slot, IR_TY_PTR),
          allocation->dst)) {
    free(suffix_sizes);
    return ir_val_none();
  }
  free(suffix_sizes);
  return ir_val_imm(IR_TY_I32, 0);
}
