#include "hir_ir_builder_internal.h"
#include "function_type_lowering.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../semantic/type_identity.h"
#include "../type_layout.h"

static ir_val_t global_address(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  const psx_hir_symbol_t *symbol = hir_ir_resolved_global_symbol(context, node);
  if (!symbol) return ir_val_none();
  size_t name_length = 0;
  const char *name = psx_hir_symbol_name(symbol, &name_length);
  int result = hir_ir_new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(
      psx_hir_symbol_is_thread_local(symbol)
          ? IR_LOAD_TLS_SYM : IR_LOAD_SYM);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(result, IR_TY_PTR);
  load->sym = (char *)name;
  load->sym_len = (int)name_length;
  load->is_external_symbol = psx_hir_symbol_is_extern(symbol) ? 1 : 0;
  if (!hir_ir_append_instruction(context, load)) return ir_val_none();
  return load->dst;
}

int hir_ir_node_is_lvalue(const psx_hir_node_t *node) {
  if (!node) return 0;
  psx_hir_node_kind_t kind = psx_hir_node_kind(node);
  return kind == PSX_HIR_LOCAL || kind == PSX_HIR_GLOBAL ||
         kind == PSX_HIR_DEREF || kind == PSX_HIR_SUBSCRIPT ||
         kind == PSX_HIR_MEMBER_ACCESS || kind == PSX_HIR_STMT_EXPR;
}

static int semantic_type_is_pointer_like(const psx_type_shape_t *type) {
  return type && (type->kind == PSX_TYPE_POINTER ||
                  type->kind == PSX_TYPE_ARRAY);
}

static ir_val_t pointer_stride_value(
    hir_ir_context_t *context, const psx_hir_node_t *pointer) {
  int stride_offset = psx_hir_node_vla_stride_frame_offset(pointer);
  if (stride_offset != 0) {
    int slot_size = psx_hir_node_vla_stride_slot_size(pointer);
    if (slot_size < 8) return hir_ir_unsupported_expr(context);
    int stride_pointer = hir_ir_local_storage_address(
        context, stride_offset, slot_size, 8);
    if (stride_pointer < 0) return ir_val_none();
    int stride_vreg = hir_ir_new_vreg(context);
    if (stride_vreg < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(stride_vreg, IR_TY_I64);
    load->src1 = ir_val_vreg(stride_pointer, IR_TY_PTR);
    if (!hir_ir_append_instruction(context, load)) return ir_val_none();
    return load->dst;
  }

  psx_qual_type_t element_type = psx_semantic_type_table_base(
      context->options->semantic_types,
      psx_hir_node_qual_type(pointer).type_id);
  int stride_bytes =
      psx_type_layout_sizeof(context->options->semantic_types,
                        context->options->record_layouts, element_type.type_id,
                        ag_target_info_data_layout(context->options->target));
  if (stride_bytes <= 0) return hir_ir_unsupported_expr(context);
  return ir_val_imm(IR_TY_I64, stride_bytes);
}

static ir_val_t pointer_with_element_offset(
    hir_ir_context_t *context, const psx_hir_node_t *pointer_node,
    ir_val_t pointer, const psx_hir_node_t *index_node,
    ir_val_t index, int subtract) {
  ir_mir_type_info_t index_type =
      hir_ir_classify_node_type(context, index_node);
  if (pointer.type != IR_TY_PTR ||
      index_type.type_class != IR_MIR_TYPE_INTEGER ||
      !hir_ir_is_integer_type(index.type))
    return hir_ir_unsupported_expr(context);
  index = hir_ir_emit_integer_width_conversion(
      context, index, IR_TY_I64, !index_type.is_unsigned);
  ir_val_t stride = pointer_stride_value(context, pointer_node);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_val_t byte_offset = hir_ir_emit_integer_binary(
      context, IR_MUL, index, stride, IR_TY_I64);
  if (subtract && context->status == IR_HIR_BUILD_OK) {
    byte_offset = hir_ir_emit_integer_binary(
        context, IR_SUB, ir_val_imm(IR_TY_I64, 0),
        byte_offset, IR_TY_I64);
  }
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  int result = hir_ir_new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *lea = ir_inst_new(IR_LEA);
  if (!lea) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  lea->dst = ir_val_vreg(result, IR_TY_PTR);
  lea->src1 = pointer;
  lea->src2 = byte_offset;
  if (!hir_ir_append_instruction(context, lea)) return ir_val_none();
  return lea->dst;
}

static int try_build_pointer_arithmetic(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    const psx_hir_node_t *lhs, const psx_hir_node_t *rhs,
    ir_mir_type_info_t result_type, ir_val_t *result) {
  psx_hir_node_kind_t kind = psx_hir_node_kind(node);
  if (kind != PSX_HIR_ADD && kind != PSX_HIR_SUB) return 0;
  psx_type_shape_t left_type = {0};
  psx_type_shape_t right_type = {0};
  int left_pointer =
      hir_ir_node_type_shape(context, lhs, &left_type) &&
      semantic_type_is_pointer_like(&left_type);
  int right_pointer =
      hir_ir_node_type_shape(context, rhs, &right_type) &&
      semantic_type_is_pointer_like(&right_type);
  if (!left_pointer && !right_pointer) return 0;

  ir_val_t left = hir_ir_build_expr(context, lhs);
  ir_val_t right = hir_ir_build_expr(context, rhs);
  if (context->status != IR_HIR_BUILD_OK) {
    *result = ir_val_none();
    return 1;
  }
  if (left_pointer && right_pointer) {
    if (kind != PSX_HIR_SUB ||
        left.type != IR_TY_PTR || right.type != IR_TY_PTR ||
        result_type.type_class != IR_MIR_TYPE_INTEGER) {
      *result = hir_ir_unsupported_expr(context);
      return 1;
    }
    ir_val_t byte_difference = hir_ir_emit_integer_binary(
        context, IR_SUB, left, right, IR_TY_I64);
    ir_val_t stride = pointer_stride_value(context, lhs);
    if (context->status == IR_HIR_BUILD_OK &&
        !(stride.id == IR_VAL_IMM && stride.imm == 1)) {
      byte_difference = hir_ir_emit_integer_binary(
          context, IR_DIV, byte_difference, stride, IR_TY_I64);
    }
    if (context->status == IR_HIR_BUILD_OK) {
      byte_difference = hir_ir_emit_integer_width_conversion(
          context, byte_difference,
          hir_ir_scalar_storage_type(result_type), 1);
    }
    *result = byte_difference;
    return 1;
  }
  if (left_pointer) {
    *result = pointer_with_element_offset(
        context, lhs, left, rhs, right, kind == PSX_HIR_SUB);
    return 1;
  }
  if (kind != PSX_HIR_ADD) {
    *result = hir_ir_unsupported_expr(context);
    return 1;
  }
  *result = pointer_with_element_offset(
      context, rhs, right, lhs, left, 0);
  return 1;
}

static ir_val_t subscript_address(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  const psx_hir_node_t *base = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *index = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  if (!base || !index) return hir_ir_unsupported_expr(context);

  psx_qual_type_t base_qual_type = psx_hir_node_qual_type(base);
  psx_type_shape_t base_type = {0};
  if (!hir_ir_type_shape(context, base_qual_type.type_id, &base_type) ||
      (base_type.kind != PSX_TYPE_POINTER &&
       base_type.kind != PSX_TYPE_ARRAY))
    return hir_ir_unsupported_expr(context);

  ir_val_t base_pointer = hir_ir_build_expr(context, base);
  ir_val_t index_value = hir_ir_build_expr(context, index);
  ir_mir_type_info_t index_type = hir_ir_classify_node_type(context, index);
  if (context->status != IR_HIR_BUILD_OK ||
      base_pointer.type != IR_TY_PTR ||
      index_type.type_class != IR_MIR_TYPE_INTEGER ||
      !hir_ir_is_integer_type(index_value.type))
    return hir_ir_unsupported_expr(context);
  index_value = hir_ir_emit_integer_width_conversion(
      context, index_value, IR_TY_I64, !index_type.is_unsigned);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();

  ir_val_t stride = pointer_stride_value(context, base);

  ir_val_t byte_offset = hir_ir_emit_integer_binary(
      context, IR_MUL, index_value, stride, IR_TY_I64);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  int result = hir_ir_new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *lea = ir_inst_new(IR_LEA);
  if (!lea) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  lea->dst = ir_val_vreg(result, IR_TY_PTR);
  lea->src1 = base_pointer;
  lea->src2 = byte_offset;
  if (!hir_ir_append_instruction(context, lea)) return ir_val_none();
  return lea->dst;
}

static ir_val_t member_address(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  const psx_hir_node_t *base = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  if (!base) return hir_ir_unsupported_expr(context);
  ir_val_t base_address =
      psx_hir_node_member_from_pointer(node)
          ? hir_ir_build_expr(context, base)
          : hir_ir_aggregate_value_address(context, base);
  if (context->status != IR_HIR_BUILD_OK ||
      base_address.type != IR_TY_PTR)
    return hir_ir_unsupported_expr(context);
  return hir_ir_pointer_with_offset(
      context, base_address, psx_hir_node_member_offset(node));
}

ir_val_t hir_ir_lvalue_address(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  if (!node) return hir_ir_unsupported_expr(context);
  if (psx_hir_node_kind(node) == PSX_HIR_STMT_EXPR) {
    const psx_hir_node_t *prefix = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    const psx_hir_node_t *value = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_RHS, 0);
    if (!prefix || psx_hir_node_kind(prefix) != PSX_HIR_BLOCK ||
        !value || !hir_ir_build_statement(context, prefix))
      return hir_ir_unsupported_expr(context);
    return hir_ir_lvalue_address(context, value);
  }
  if (psx_hir_node_kind(node) == PSX_HIR_LOCAL) {
    int pointer = hir_ir_local_address(context, node);
    return pointer < 0 ? ir_val_none()
                       : ir_val_vreg(pointer, IR_TY_PTR);
  }
  if (psx_hir_node_kind(node) == PSX_HIR_GLOBAL)
    return global_address(context, node);
  if (psx_hir_node_kind(node) == PSX_HIR_DEREF) {
    const psx_hir_node_t *pointer = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    if (!pointer) return hir_ir_unsupported_expr(context);
    ir_val_t result = hir_ir_build_expr(context, pointer);
    if (context->status != IR_HIR_BUILD_OK || result.type != IR_TY_PTR)
      return hir_ir_unsupported_expr(context);
    return result;
  }
  if (psx_hir_node_kind(node) == PSX_HIR_SUBSCRIPT)
    return subscript_address(context, node);
  if (psx_hir_node_kind(node) == PSX_HIR_MEMBER_ACCESS)
    return member_address(context, node);
  return hir_ir_unsupported_expr(context);
}

ir_val_t hir_ir_pointer_with_offset(
    hir_ir_context_t *context, ir_val_t base, int offset) {
  if (base.type != IR_TY_PTR || offset < 0)
    return hir_ir_unsupported_expr(context);
  if (offset == 0) return base;
  int pointer = hir_ir_new_vreg(context);
  if (pointer < 0) return ir_val_none();
  ir_inst_t *lea = ir_inst_new(IR_LEA);
  if (!lea) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  lea->dst = ir_val_vreg(pointer, IR_TY_PTR);
  lea->src1 = base;
  lea->src2 = ir_val_imm(IR_TY_I64, offset);
  if (!hir_ir_append_instruction(context, lea)) return ir_val_none();
  return lea->dst;
}

static ir_val_t build_complex_assignment(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t target_type) {
  const psx_hir_node_t *target = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *source_node = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  if (!target || !source_node || !hir_ir_is_complex_type(target_type) ||
      !hir_ir_node_is_lvalue(target))
    return hir_ir_unsupported_expr(context);
  ir_val_t destination = hir_ir_lvalue_address(context, target);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_mir_type_info_t source_type = hir_ir_classify_node_type(
      context, source_node);
  if (hir_ir_is_complex_type(source_type)) {
    ir_val_t source = hir_ir_materialize_complex_operand(
        context, source_node, target_type);
    if (context->status != IR_HIR_BUILD_OK || source.type != IR_TY_PTR)
      return hir_ir_unsupported_expr(context);
    ir_inst_t *copy = ir_inst_new(IR_MEMCPY);
    if (!copy) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    copy->src1 = destination;
    copy->src2 = source;
    copy->alloca_size = target_type.source_size;
    if (!hir_ir_append_instruction(context, copy)) return ir_val_none();
    return destination;
  }
  if (!hir_ir_is_scalar_value_type(source_type))
    return hir_ir_unsupported_expr(context);
  ir_val_t real = hir_ir_build_expr(context, source_node);
  ir_mir_type_info_t component_type = {
      .type = target_type.type,
      .type_class = IR_MIR_TYPE_FLOAT,
      .source_size = ir_type_fixed_size(target_type.type),
  };
  if (context->status == IR_HIR_BUILD_OK)
    real = hir_ir_coerce_direct_value(
        context, real, source_type, component_type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_inst_t *store_real = ir_inst_new(IR_STORE);
  if (!store_real) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  store_real->src1 = destination;
  store_real->src2 = real;
  if (!hir_ir_append_instruction(context, store_real)) return ir_val_none();
  int zero_vreg = hir_ir_new_vreg(context);
  if (zero_vreg < 0) return ir_val_none();
  ir_inst_t *zero = ir_inst_new(IR_LOAD_FP_IMM);
  if (!zero) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  zero->dst = ir_val_vreg(zero_vreg, target_type.type);
  zero->src1 = ir_val_fp_imm(target_type.type, 0.0);
  if (!hir_ir_append_instruction(context, zero)) return ir_val_none();
  ir_val_t imaginary_pointer = hir_ir_pointer_with_offset(
      context, destination, component_type.source_size);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_inst_t *store_imaginary = ir_inst_new(IR_STORE);
  if (!store_imaginary) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  store_imaginary->src1 = imaginary_pointer;
  store_imaginary->src2 = zero->dst;
  if (!hir_ir_append_instruction(context, store_imaginary)) return ir_val_none();
  return destination;
}

static ir_val_t build_complex_comparison(
    hir_ir_context_t *context, psx_hir_node_kind_t kind,
    ir_val_t left, ir_val_t right, ir_mir_type_info_t type) {
  if ((kind != PSX_HIR_EQ && kind != PSX_HIR_NE) ||
      left.type != IR_TY_PTR || right.type != IR_TY_PTR ||
      !hir_ir_is_complex_type(type))
    return hir_ir_unsupported_expr(context);
  int half = ir_type_fixed_size(type.type);
  ir_val_t left_imaginary = hir_ir_pointer_with_offset(context, left, half);
  ir_val_t right_imaginary = hir_ir_pointer_with_offset(context, right, half);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_val_t values[4];
  ir_val_t pointers[4] = {left, left_imaginary, right, right_imaginary};
  for (int i = 0; i < 4; i++) {
    int value = hir_ir_new_vreg(context);
    if (value < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(value, type.type);
    load->src1 = pointers[i];
    if (!hir_ir_append_instruction(context, load)) return ir_val_none();
    values[i] = load->dst;
  }
  ir_op_t compare_op = kind == PSX_HIR_EQ ? IR_FEQ : IR_FNE;
  ir_op_t combine_op = kind == PSX_HIR_EQ ? IR_AND : IR_OR;
  ir_val_t comparisons[2];
  for (int i = 0; i < 2; i++) {
    int comparison = hir_ir_new_vreg(context);
    if (comparison < 0) return ir_val_none();
    ir_inst_t *compare = ir_inst_new(compare_op);
    if (!compare) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    compare->dst = ir_val_vreg(comparison, IR_TY_I32);
    compare->src1 = values[i];
    compare->src2 = values[i + 2];
    if (!hir_ir_append_instruction(context, compare)) return ir_val_none();
    comparisons[i] = compare->dst;
  }
  return hir_ir_emit_integer_binary(
      context, combine_op, comparisons[0], comparisons[1], IR_TY_I32);
}

static ir_val_t load_direct_value(
    hir_ir_context_t *context, ir_val_t pointer, ir_type_t type) {
  if (pointer.type != IR_TY_PTR) return hir_ir_unsupported_expr(context);
  int value = hir_ir_new_vreg(context);
  if (value < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_LOAD);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(value, type);
  load->src1 = pointer;
  if (!hir_ir_append_instruction(context, load)) return ir_val_none();
  return load->dst;
}

int hir_ir_store_direct_value(
    hir_ir_context_t *context, ir_val_t pointer, ir_val_t value) {
  if (pointer.type != IR_TY_PTR) {
    context->status = IR_HIR_BUILD_UNSUPPORTED;
    return 0;
  }
  ir_inst_t *store = ir_inst_new(IR_STORE);
  if (!store) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  store->src1 = pointer;
  store->src2 = value;
  return hir_ir_append_instruction(context, store);
}

static ir_val_t emit_float_binary(
    hir_ir_context_t *context, ir_op_t op, ir_val_t left,
    ir_val_t right, ir_type_t type) {
  int result = hir_ir_new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *instruction = ir_inst_new(op);
  if (!instruction) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  instruction->dst = ir_val_vreg(result, type);
  instruction->src1 = left;
  instruction->src2 = right;
  if (!hir_ir_append_instruction(context, instruction)) return ir_val_none();
  return instruction->dst;
}

static ir_val_t convert_complex_pointer(
    hir_ir_context_t *context, ir_val_t source,
    ir_mir_type_info_t source_type,
    ir_mir_type_info_t target_type) {
  if (source.type != IR_TY_PTR ||
      !hir_ir_is_complex_type(source_type) ||
      !hir_ir_is_complex_type(target_type))
    return hir_ir_unsupported_expr(context);
  if (source_type.type == target_type.type &&
      source_type.source_size == target_type.source_size)
    return source;

  int source_half = ir_type_fixed_size(source_type.type);
  int target_half = ir_type_fixed_size(target_type.type);
  int slot = hir_ir_allocate_scalar_temp(
      context, target_type.source_size,
      target_half >= 8 ? 8 : 4);
  if (slot < 0) return ir_val_none();
  ir_val_t destination = ir_val_vreg(slot, IR_TY_PTR);
  ir_mir_type_info_t source_component = {
      .type = source_type.type,
      .type_class = IR_MIR_TYPE_FLOAT,
      .source_size = source_half,
  };
  ir_mir_type_info_t target_component = {
      .type = target_type.type,
      .type_class = IR_MIR_TYPE_FLOAT,
      .source_size = target_half,
  };
  for (int part = 0; part < 2; part++) {
    ir_val_t source_pointer = source;
    ir_val_t destination_pointer = destination;
    if (part == 1) {
      source_pointer = hir_ir_pointer_with_offset(
          context, source, source_half);
      destination_pointer = hir_ir_pointer_with_offset(
          context, destination, target_half);
    }
    if (context->status != IR_HIR_BUILD_OK)
      return ir_val_none();
    ir_val_t component = load_direct_value(
        context, source_pointer, source_type.type);
    if (context->status == IR_HIR_BUILD_OK)
      component = hir_ir_coerce_direct_value(
          context, component, source_component, target_component);
    if (context->status != IR_HIR_BUILD_OK ||
        !hir_ir_store_direct_value(
            context, destination_pointer, component))
      return ir_val_none();
  }
  return destination;
}

static ir_val_t materialize_direct_value_as_complex(
    hir_ir_context_t *context, ir_val_t real,
    ir_mir_type_info_t source_type,
    ir_mir_type_info_t target_type) {
  if (!hir_ir_is_scalar_value_type(source_type) ||
      !hir_ir_is_complex_type(target_type))
    return hir_ir_unsupported_expr(context);
  int half = ir_type_fixed_size(target_type.type);
  int slot = hir_ir_allocate_scalar_temp(
      context, target_type.source_size, half >= 8 ? 8 : 4);
  if (slot < 0) return ir_val_none();
  ir_val_t destination = ir_val_vreg(slot, IR_TY_PTR);
  ir_mir_type_info_t component_type = {
      .type = target_type.type,
      .type_class = IR_MIR_TYPE_FLOAT,
      .source_size = half,
  };
  if (context->status == IR_HIR_BUILD_OK)
    real = hir_ir_coerce_direct_value(
        context, real, source_type, component_type);
  if (context->status != IR_HIR_BUILD_OK ||
      !hir_ir_store_direct_value(context, destination, real))
    return ir_val_none();
  int zero_vreg = hir_ir_new_vreg(context);
  if (zero_vreg < 0) return ir_val_none();
  ir_inst_t *zero = ir_inst_new(IR_LOAD_FP_IMM);
  if (!zero) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  zero->dst = ir_val_vreg(zero_vreg, target_type.type);
  zero->src1 = ir_val_fp_imm(target_type.type, 0.0);
  if (!hir_ir_append_instruction(context, zero)) return ir_val_none();
  ir_val_t imaginary = hir_ir_pointer_with_offset(context, destination, half);
  if (context->status != IR_HIR_BUILD_OK ||
      !hir_ir_store_direct_value(context, imaginary, zero->dst))
    return ir_val_none();
  return destination;
}

ir_val_t hir_ir_materialize_complex_operand(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t target_type) {
  ir_mir_type_info_t source_type = hir_ir_classify_node_type(context, node);
  ir_val_t source = hir_ir_build_expr(context, node);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  if (hir_ir_is_complex_type(source_type))
    return convert_complex_pointer(
        context, source, source_type, target_type);
  return materialize_direct_value_as_complex(
      context, source, source_type, target_type);
}

static ir_val_t emit_complex_binary_values(
    hir_ir_context_t *context, psx_hir_node_kind_t kind,
    ir_val_t left, ir_val_t right,
    ir_mir_type_info_t type) {
  if (!hir_ir_is_complex_type(type) ||
      left.type != IR_TY_PTR || right.type != IR_TY_PTR ||
      (kind != PSX_HIR_ADD && kind != PSX_HIR_SUB &&
       kind != PSX_HIR_MUL && kind != PSX_HIR_DIV))
    return hir_ir_unsupported_expr(context);
  int half = ir_type_fixed_size(type.type);
  ir_val_t left_imaginary = hir_ir_pointer_with_offset(context, left, half);
  ir_val_t right_imaginary = hir_ir_pointer_with_offset(context, right, half);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_val_t lr = load_direct_value(context, left, type.type);
  ir_val_t li = load_direct_value(context, left_imaginary, type.type);
  ir_val_t rr = load_direct_value(context, right, type.type);
  ir_val_t ri = load_direct_value(context, right_imaginary, type.type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();

  ir_val_t real;
  ir_val_t imaginary;
  if (kind == PSX_HIR_ADD || kind == PSX_HIR_SUB) {
    ir_op_t op = kind == PSX_HIR_ADD ? IR_FADD : IR_FSUB;
    real = emit_float_binary(context, op, lr, rr, type.type);
    imaginary = emit_float_binary(context, op, li, ri, type.type);
  } else if (kind == PSX_HIR_MUL) {
    ir_val_t lr_rr = emit_float_binary(
        context, IR_FMUL, lr, rr, type.type);
    ir_val_t li_ri = emit_float_binary(
        context, IR_FMUL, li, ri, type.type);
    real = emit_float_binary(
        context, IR_FSUB, lr_rr, li_ri, type.type);
    ir_val_t lr_ri = emit_float_binary(
        context, IR_FMUL, lr, ri, type.type);
    ir_val_t li_rr = emit_float_binary(
        context, IR_FMUL, li, rr, type.type);
    imaginary = emit_float_binary(
        context, IR_FADD, lr_ri, li_rr, type.type);
  } else {
    ir_val_t rr_squared = emit_float_binary(
        context, IR_FMUL, rr, rr, type.type);
    ir_val_t ri_squared = emit_float_binary(
        context, IR_FMUL, ri, ri, type.type);
    ir_val_t denominator = emit_float_binary(
        context, IR_FADD, rr_squared, ri_squared, type.type);
    ir_val_t lr_rr = emit_float_binary(
        context, IR_FMUL, lr, rr, type.type);
    ir_val_t li_ri = emit_float_binary(
        context, IR_FMUL, li, ri, type.type);
    ir_val_t real_numerator = emit_float_binary(
        context, IR_FADD, lr_rr, li_ri, type.type);
    real = emit_float_binary(
        context, IR_FDIV, real_numerator, denominator, type.type);
    ir_val_t li_rr = emit_float_binary(
        context, IR_FMUL, li, rr, type.type);
    ir_val_t lr_ri = emit_float_binary(
        context, IR_FMUL, lr, ri, type.type);
    ir_val_t imaginary_numerator = emit_float_binary(
        context, IR_FSUB, li_rr, lr_ri, type.type);
    imaginary = emit_float_binary(
        context, IR_FDIV, imaginary_numerator, denominator, type.type);
  }
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  int slot = hir_ir_allocate_scalar_temp(
      context, type.source_size, half >= 8 ? 8 : 4);
  if (slot < 0) return ir_val_none();
  ir_val_t destination = ir_val_vreg(slot, IR_TY_PTR);
  ir_val_t imaginary_destination = hir_ir_pointer_with_offset(
      context, destination, half);
  if (context->status != IR_HIR_BUILD_OK ||
      !hir_ir_store_direct_value(context, destination, real) ||
      !hir_ir_store_direct_value(context, imaginary_destination, imaginary))
    return ir_val_none();
  return destination;
}

static ir_val_t build_complex_binary(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t type) {
  psx_hir_node_kind_t kind = psx_hir_node_kind(node);
  const psx_hir_node_t *left_node = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *right_node = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  if (!left_node || !right_node || !hir_ir_is_complex_type(type))
    return hir_ir_unsupported_expr(context);
  ir_mir_type_info_t left_type = hir_ir_classify_node_type(context, left_node);
  ir_mir_type_info_t right_type = hir_ir_classify_node_type(context, right_node);
  if ((!hir_ir_is_complex_type(left_type) &&
       !hir_ir_is_scalar_value_type(left_type)) ||
      (!hir_ir_is_complex_type(right_type) &&
       !hir_ir_is_scalar_value_type(right_type)))
    return hir_ir_unsupported_expr(context);
  ir_val_t left = hir_ir_materialize_complex_operand(
      context, left_node, type);
  ir_val_t right = hir_ir_materialize_complex_operand(
      context, right_node, type);
  if (context->status != IR_HIR_BUILD_OK)
    return ir_val_none();
  return emit_complex_binary_values(
      context, kind, left, right, type);
}

static ir_val_t emit_float_negate(
    hir_ir_context_t *context, ir_val_t value, ir_type_t type) {
  int result = hir_ir_new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *negate = ir_inst_new(IR_FNEG);
  if (!negate) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  negate->dst = ir_val_vreg(result, type);
  negate->src1 = value;
  if (!hir_ir_append_instruction(context, negate)) return ir_val_none();
  return negate->dst;
}

static ir_val_t build_complex_negate(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t type) {
  const psx_hir_node_t *operand = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  if (!operand || !hir_ir_is_complex_type(type))
    return hir_ir_unsupported_expr(context);
  ir_val_t source = hir_ir_materialize_complex_operand(
      context, operand, type);
  if (context->status != IR_HIR_BUILD_OK ||
      source.type != IR_TY_PTR)
    return hir_ir_unsupported_expr(context);
  int half = ir_type_fixed_size(type.type);
  ir_val_t imaginary_source = hir_ir_pointer_with_offset(
      context, source, half);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_val_t real = load_direct_value(
      context, source, type.type);
  ir_val_t imaginary = load_direct_value(
      context, imaginary_source, type.type);
  if (context->status == IR_HIR_BUILD_OK)
    real = emit_float_negate(context, real, type.type);
  if (context->status == IR_HIR_BUILD_OK)
    imaginary = emit_float_negate(
        context, imaginary, type.type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  int slot = hir_ir_allocate_scalar_temp(
      context, type.source_size, half >= 8 ? 8 : 4);
  if (slot < 0) return ir_val_none();
  ir_val_t destination = ir_val_vreg(slot, IR_TY_PTR);
  ir_val_t imaginary_destination = hir_ir_pointer_with_offset(
      context, destination, half);
  if (context->status != IR_HIR_BUILD_OK ||
      !hir_ir_store_direct_value(context, destination, real) ||
      !hir_ir_store_direct_value(
          context, imaginary_destination, imaginary))
    return ir_val_none();
  return destination;
}

static ir_val_t build_complex_component(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t result_type) {
  const psx_hir_node_t *operand = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  int is_real = psx_hir_node_kind(node) == PSX_HIR_CREAL;
  if (!operand || !hir_ir_is_direct_value_type(result_type))
    return hir_ir_unsupported_expr(context);
  ir_mir_type_info_t operand_type = hir_ir_classify_node_type(
      context, operand);
  if (!hir_ir_is_complex_type(operand_type)) {
    if (!is_real) {
      if (!hir_ir_is_float_value_type(result_type))
        return ir_val_imm(result_type.type, 0);
      int zero_vreg = hir_ir_new_vreg(context);
      if (zero_vreg < 0) return ir_val_none();
      ir_inst_t *zero = ir_inst_new(IR_LOAD_FP_IMM);
      if (!zero) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return ir_val_none();
      }
      zero->dst = ir_val_vreg(zero_vreg, result_type.type);
      zero->src1 = ir_val_fp_imm(result_type.type, 0.0);
      if (!hir_ir_append_instruction(context, zero)) return ir_val_none();
      return zero->dst;
    }
    ir_val_t value = hir_ir_build_expr(context, operand);
    if (context->status == IR_HIR_BUILD_OK)
      value = hir_ir_coerce_direct_value(
          context, value, operand_type, result_type);
    return value;
  }
  if (!hir_ir_is_float_value_type(result_type))
    return hir_ir_unsupported_expr(context);
  if (operand_type.type != result_type.type)
    return hir_ir_unsupported_expr(context);
  ir_val_t value = hir_ir_build_expr(context, operand);
  if (context->status != IR_HIR_BUILD_OK ||
      value.type != IR_TY_PTR)
    return hir_ir_unsupported_expr(context);
  if (!is_real)
    value = hir_ir_pointer_with_offset(
        context, value, ir_type_fixed_size(result_type.type));
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  return load_direct_value(context, value, result_type.type);
}

static ir_val_t build_scalar_negate(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t type) {
  const psx_hir_node_t *operand = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  if (!operand || !hir_ir_is_direct_value_type(type))
    return hir_ir_unsupported_expr(context);
  ir_val_t value = hir_ir_build_expr(context, operand);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  value = hir_ir_coerce_direct_value(
      context, value, hir_ir_classify_node_type(context, operand), type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  if (hir_ir_is_float_value_type(type))
    return emit_float_negate(context, value, type.type);
  if (type.type_class != IR_MIR_TYPE_INTEGER)
    return hir_ir_unsupported_expr(context);
  return hir_ir_emit_integer_binary(
      context, IR_SUB, ir_val_imm(type.type, 0), value, type.type);
}

static ir_val_t build_unary_plus(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t type) {
  const psx_hir_node_t *operand = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  if (!operand) return hir_ir_unsupported_expr(context);
  if (hir_ir_is_complex_type(type))
    return hir_ir_materialize_complex_operand(context, operand, type);
  if (!hir_ir_is_direct_value_type(type))
    return hir_ir_unsupported_expr(context);
  ir_val_t value = hir_ir_build_expr(context, operand);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  return hir_ir_coerce_direct_value(
      context, value, hir_ir_classify_node_type(context, operand), type);
}

static ir_val_t build_bitwise_not(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t type) {
  const psx_hir_node_t *operand = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  if (!operand || type.type_class != IR_MIR_TYPE_INTEGER)
    return hir_ir_unsupported_expr(context);
  ir_val_t value = hir_ir_build_expr(context, operand);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  value = hir_ir_coerce_direct_value(
      context, value, hir_ir_classify_node_type(context, operand), type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  return hir_ir_emit_integer_binary(
      context, IR_XOR, value,
      ir_val_imm(type.type, -1), type.type);
}

ir_val_t hir_ir_emit_integer_width_conversion(
    hir_ir_context_t *context, ir_val_t value, ir_type_t target,
    int sign_extend) {
  if (!hir_ir_is_integer_type(value.type) || !hir_ir_is_integer_type(target))
    return hir_ir_unsupported_expr(context);
  if (value.type == target) return value;
  if (value.id == IR_VAL_IMM) {
    value.type = target;
    return value;
  }
  int result = hir_ir_new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *conversion = ir_inst_new(
      ir_type_fixed_size(value.type) > ir_type_fixed_size(target)
          ? IR_TRUNC : sign_extend ? IR_SEXT : IR_ZEXT);
  if (!conversion) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  conversion->dst = ir_val_vreg(result, target);
  conversion->src1 = value;
  if (!hir_ir_append_instruction(context, conversion)) return ir_val_none();
  return conversion->dst;
}

ir_val_t hir_ir_emit_integer_binary(
    hir_ir_context_t *context, ir_op_t op, ir_val_t left,
    ir_val_t right, ir_type_t type) {
  int result = hir_ir_new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *instruction = ir_inst_new(op);
  if (!instruction) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  instruction->dst = ir_val_vreg(result, type);
  instruction->src1 = left;
  instruction->src2 = right;
  if (!hir_ir_append_instruction(context, instruction)) return ir_val_none();
  return instruction->dst;
}

static ir_val_t coerce_explicit_cast_value(
    hir_ir_context_t *context, ir_val_t value,
    ir_mir_type_info_t source, ir_mir_type_info_t target,
    psx_qual_type_t target_qual_type) {
  psx_type_shape_t semantic_target = {0};
  if (!hir_ir_type_shape(
          context, target_qual_type.type_id, &semantic_target))
    return hir_ir_unsupported_expr(context);
  if (semantic_target.kind == PSX_TYPE_BOOL) {
    return hir_ir_coerce_direct_value_to_qual_type(
        context, value, source, target, target_qual_type);
  }
  if (semantic_target.kind != PSX_TYPE_INTEGER ||
      target.source_size >= 4)
    return hir_ir_coerce_direct_value(context, value, source, target);

  ir_mir_type_info_t promoted_target = target;
  promoted_target.type = IR_TY_I32;
  promoted_target.type_class = IR_MIR_TYPE_INTEGER;
  promoted_target.source_size = 4;
  value = hir_ir_coerce_direct_value(
      context, value, source, promoted_target);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  if (value.id == IR_VAL_IMM) {
    value.imm = hir_ir_normalize_integer_immediate(
        value.imm, target.source_size, target.is_unsigned);
    value.type = hir_ir_integer_storage_type(target);
    return value;
  }

  int shift = 32 - target.source_size * 8;
  ir_val_t shifted = hir_ir_emit_integer_binary(
      context, IR_SHL, value,
      ir_val_imm(IR_TY_I32, shift), IR_TY_I32);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  shifted = hir_ir_emit_integer_binary(
      context, target.is_unsigned ? IR_LSR : IR_SHR, shifted,
      ir_val_imm(IR_TY_I32, shift), IR_TY_I32);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  return hir_ir_emit_integer_width_conversion(
      context, shifted, hir_ir_integer_storage_type(target), 0);
}

static ir_val_t bitfield_constant(
    hir_ir_context_t *context, ir_type_t type, uint64_t value) {
  if (type != IR_TY_I64)
    return ir_val_imm(type, (long long)value);
  int result = hir_ir_new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_LOAD_IMM);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(result, type);
  load->src1 = ir_val_imm(type, (long long)value);
  if (!hir_ir_append_instruction(context, load)) return ir_val_none();
  return load->dst;
}

static int valid_bitfield(int bit_width, int bit_offset) {
  return bit_width > 0 && bit_width <= 64 && bit_offset >= 0 &&
         bit_offset < 64 && bit_width + bit_offset <= 64;
}

static int valid_bitfield_storage(
    ir_type_t memory_type, int bit_width, int bit_offset) {
  int storage_size = ir_type_fixed_size(memory_type);
  return hir_ir_is_integer_type(memory_type) && storage_size > 0 &&
         valid_bitfield(bit_width, bit_offset) &&
         bit_width + bit_offset <= storage_size * 8;
}

static ir_val_t emit_bitfield_load(
    hir_ir_context_t *context, ir_val_t pointer, int bit_width,
    int bit_offset, int is_signed, ir_type_t memory_type,
    ir_type_t result_type) {
  if (!valid_bitfield_storage(memory_type, bit_width, bit_offset) ||
      !hir_ir_is_integer_type(result_type))
    return hir_ir_unsupported_expr(context);
  ir_type_t unit_type = memory_type == IR_TY_I64
                            ? IR_TY_I64 : IR_TY_I32;
  int loaded_vreg = hir_ir_new_vreg(context);
  if (loaded_vreg < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_LOAD);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(loaded_vreg, memory_type);
  load->src1 = pointer;
  load->is_unsigned = 1;
  if (!hir_ir_append_instruction(context, load)) return ir_val_none();
  ir_val_t current = hir_ir_emit_integer_width_conversion(
      context, load->dst, unit_type, 1);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  if (bit_offset > 0) {
    current = hir_ir_emit_integer_binary(
        context, IR_SHR, current,
        ir_val_imm(unit_type, bit_offset), unit_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  }
  uint64_t mask = bit_width == 64
                      ? UINT64_MAX : (UINT64_C(1) << bit_width) - 1;
  current = hir_ir_emit_integer_binary(
      context, IR_AND, current,
      bitfield_constant(context, unit_type, mask), unit_type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  if (is_signed && bit_width < (unit_type == IR_TY_I64 ? 64 : 32)) {
    uint64_t sign_bit = UINT64_C(1) << (bit_width - 1);
    current = hir_ir_emit_integer_binary(
        context, IR_XOR, current,
        bitfield_constant(context, unit_type, sign_bit), unit_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    current = hir_ir_emit_integer_binary(
        context, IR_SUB, current,
        bitfield_constant(context, unit_type, sign_bit), unit_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  }
  return hir_ir_emit_integer_width_conversion(
      context, current, result_type, is_signed);
}

static ir_val_t emit_bitfield_store(
    hir_ir_context_t *context, ir_val_t pointer, ir_val_t value,
    int bit_width, int bit_offset, ir_type_t memory_type) {
  if (!valid_bitfield_storage(memory_type, bit_width, bit_offset) ||
      !hir_ir_is_integer_type(value.type))
    return hir_ir_unsupported_expr(context);
  ir_type_t unit_type = memory_type == IR_TY_I64
                            ? IR_TY_I64 : IR_TY_I32;
  ir_val_t stored_value = hir_ir_emit_integer_width_conversion(
      context, value, unit_type, 0);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  int loaded_vreg = hir_ir_new_vreg(context);
  if (loaded_vreg < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_LOAD);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(loaded_vreg, memory_type);
  load->src1 = pointer;
  load->is_unsigned = 1;
  if (!hir_ir_append_instruction(context, load)) return ir_val_none();
  ir_val_t loaded_value = hir_ir_emit_integer_width_conversion(
      context, load->dst, unit_type, 1);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  uint64_t mask = bit_width == 64
                      ? UINT64_MAX : (UINT64_C(1) << bit_width) - 1;
  uint64_t shifted_mask = mask << bit_offset;
  ir_val_t cleared = hir_ir_emit_integer_binary(
      context, IR_AND, loaded_value,
      bitfield_constant(context, unit_type, ~shifted_mask), unit_type);
  ir_val_t field = hir_ir_emit_integer_binary(
      context, IR_AND, stored_value,
      bitfield_constant(context, unit_type, mask), unit_type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  if (bit_offset > 0) {
    field = hir_ir_emit_integer_binary(
        context, IR_SHL, field,
        ir_val_imm(unit_type, bit_offset), unit_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  }
  ir_val_t combined = hir_ir_emit_integer_binary(
      context, IR_OR, cleared, field, unit_type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_inst_t *store = ir_inst_new(IR_STORE);
  if (!store) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  store->src1 = pointer;
  store->src2 = hir_ir_emit_integer_width_conversion(
      context, combined, memory_type, 1);
  if (context->status != IR_HIR_BUILD_OK) {
    free(store);
    return ir_val_none();
  }
  if (!hir_ir_append_instruction(context, store)) return ir_val_none();
  return value;
}

int hir_ir_allocate_scalar_temp(
    hir_ir_context_t *context, int size, int alignment) {
  if (size <= 0 || alignment <= 0 || alignment > 16) {
    context->status = IR_HIR_BUILD_UNSUPPORTED;
    return -1;
  }
  int pointer = hir_ir_new_vreg(context);
  if (pointer < 0) return -1;
  ir_inst_t *alloca = ir_inst_new(IR_ALLOCA);
  if (!alloca) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return -1;
  }
  alloca->dst = ir_val_vreg(pointer, IR_TY_PTR);
  alloca->alloca_size = size;
  alloca->alloca_align = alignment;
  if (!hir_ir_append_instruction(context, alloca)) return -1;
  return pointer;
}

static int store_scalar_temp(
    hir_ir_context_t *context, int pointer, ir_val_t value) {
  ir_inst_t *store = ir_inst_new(IR_STORE);
  if (!store) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  store->src1 = ir_val_vreg(pointer, IR_TY_PTR);
  store->src2 = value;
  return hir_ir_append_instruction(context, store);
}

static ir_val_t load_scalar_temp(
    hir_ir_context_t *context, int pointer, ir_type_t type,
    int is_unsigned) {
  int result = hir_ir_new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_LOAD);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(result, type);
  load->src1 = ir_val_vreg(pointer, IR_TY_PTR);
  load->is_unsigned = is_unsigned ? 1 : 0;
  if (!hir_ir_append_instruction(context, load)) return ir_val_none();
  return load->dst;
}

ir_val_t hir_ir_scalar_truth_value(
    hir_ir_context_t *context, ir_val_t value) {
  if (hir_ir_is_float_type(value.type)) {
    int zero_vreg = hir_ir_new_vreg(context);
    if (zero_vreg < 0) return ir_val_none();
    ir_inst_t *zero = ir_inst_new(IR_LOAD_FP_IMM);
    if (!zero) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    zero->dst = ir_val_vreg(zero_vreg, value.type);
    zero->src1 = ir_val_fp_imm(value.type, 0.0);
    if (!hir_ir_append_instruction(context, zero)) return ir_val_none();
    int result = hir_ir_new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *compare = ir_inst_new(IR_FNE);
    if (!compare) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    compare->dst = ir_val_vreg(result, IR_TY_I32);
    compare->src1 = value;
    compare->src2 = zero->dst;
    if (!hir_ir_append_instruction(context, compare)) return ir_val_none();
    return compare->dst;
  }
  if (!hir_ir_is_integer_type(value.type) && value.type != IR_TY_PTR)
    return hir_ir_unsupported_expr(context);
  int result = hir_ir_new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *compare = ir_inst_new(IR_NE);
  if (!compare) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  compare->dst = ir_val_vreg(result, IR_TY_I32);
  compare->src1 = value;
  compare->src2 = ir_val_imm(value.type, 0);
  if (!hir_ir_append_instruction(context, compare)) return ir_val_none();
  return compare->dst;
}

static ir_val_t build_logical_not(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t result_type) {
  const psx_hir_node_t *operand = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  if (!operand || result_type.type_class != IR_MIR_TYPE_INTEGER)
    return hir_ir_unsupported_expr(context);
  ir_mir_type_info_t operand_type =
      hir_ir_classify_node_type(context, operand);
  ir_val_t value = hir_ir_build_expr(context, operand);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  if (!hir_ir_is_complex_type(operand_type)) {
    value = hir_ir_scalar_truth_value(context, value);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    return hir_ir_emit_integer_binary(
        context, IR_EQ, value,
        ir_val_imm(value.type, 0), result_type.type);
  }
  if (value.type != IR_TY_PTR) return hir_ir_unsupported_expr(context);
  int half = ir_type_fixed_size(operand_type.type);
  ir_val_t components[2] = {
      value, hir_ir_pointer_with_offset(context, value, half)};
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  int zero_vreg = hir_ir_new_vreg(context);
  if (zero_vreg < 0) return ir_val_none();
  ir_inst_t *zero = ir_inst_new(IR_LOAD_FP_IMM);
  if (!zero) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  zero->dst = ir_val_vreg(zero_vreg, operand_type.type);
  zero->src1 = ir_val_fp_imm(operand_type.type, 0.0);
  if (!hir_ir_append_instruction(context, zero)) return ir_val_none();
  ir_val_t comparisons[2];
  for (int i = 0; i < 2; i++) {
    ir_val_t component = load_direct_value(
        context, components[i], operand_type.type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    int comparison_vreg = hir_ir_new_vreg(context);
    if (comparison_vreg < 0) return ir_val_none();
    ir_inst_t *comparison = ir_inst_new(IR_FEQ);
    if (!comparison) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    comparison->dst = ir_val_vreg(comparison_vreg, result_type.type);
    comparison->src1 = component;
    comparison->src2 = zero->dst;
    if (!hir_ir_append_instruction(context, comparison))
      return ir_val_none();
    comparisons[i] = comparison->dst;
  }
  return hir_ir_emit_integer_binary(
      context, IR_AND, comparisons[0], comparisons[1],
      result_type.type);
}

static ir_val_t build_inc_dec(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t type) {
  const psx_hir_node_t *target = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  if (!target || !hir_ir_node_is_lvalue(target))
    return hir_ir_unsupported_expr(context);
  if (type.type_class != IR_MIR_TYPE_INTEGER &&
      type.type_class != IR_MIR_TYPE_POINTER &&
      !hir_ir_is_float_value_type(type))
    return hir_ir_unsupported_expr(context);

  ir_val_t pointer = hir_ir_lvalue_address(context, target);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_type_t value_type = hir_ir_scalar_storage_type(type);
  int bit_width = 0;
  int bit_offset = 0;
  int bit_is_signed = 0;
  int is_bitfield = psx_hir_node_bitfield_info(
      target, &bit_width, &bit_offset, &bit_is_signed);
  ir_val_t old_value;
  if (is_bitfield) {
    old_value = emit_bitfield_load(
        context, pointer, bit_width, bit_offset, bit_is_signed,
        value_type, value_type);
  } else {
    int old_vreg = hir_ir_new_vreg(context);
    if (old_vreg < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(old_vreg, value_type);
    load->src1 = pointer;
    load->is_unsigned = type.is_unsigned ? 1 : 0;
    if (!hir_ir_append_instruction(context, load)) return ir_val_none();
    old_value = load->dst;
  }
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();

  long long step = 1;
  ir_val_t step_value = ir_val_none();
  if (type.type_class == IR_MIR_TYPE_POINTER) {
    int stride_offset = psx_hir_node_vla_stride_frame_offset(target);
    if (stride_offset != 0) {
      int slot_size = psx_hir_node_vla_stride_slot_size(target);
      if (slot_size <= 0) slot_size = 8;
      int stride_pointer = hir_ir_local_storage_address(
          context, stride_offset, slot_size, slot_size >= 8 ? 8 : 4);
      if (stride_pointer < 0) return ir_val_none();
      int stride_vreg = hir_ir_new_vreg(context);
      if (stride_vreg < 0) return ir_val_none();
      ir_inst_t *load_stride = ir_inst_new(IR_LOAD);
      if (!load_stride) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return ir_val_none();
      }
      load_stride->dst = ir_val_vreg(stride_vreg, value_type);
      load_stride->src1 = ir_val_vreg(stride_pointer, IR_TY_PTR);
      if (!hir_ir_append_instruction(context, load_stride)) return ir_val_none();
      step_value = load_stride->dst;
    } else {
      psx_qual_type_t pointee = psx_semantic_type_table_pointee_value(
          context->options->semantic_types,
          psx_hir_node_qual_type(target).type_id);
      step = psx_type_layout_sizeof(
          context->options->semantic_types, context->options->record_layouts,
          pointee.type_id,
          ag_target_info_data_layout(context->options->target));
      if (step <= 0) return hir_ir_unsupported_expr(context);
    }
  }
  psx_hir_node_kind_t kind = psx_hir_node_kind(node);
  int is_increment = kind == PSX_HIR_PRE_INC || kind == PSX_HIR_POST_INC;
  ir_val_t new_value;
  if (hir_ir_is_float_value_type(type)) {
    int one_vreg = hir_ir_new_vreg(context);
    if (one_vreg < 0) return ir_val_none();
    ir_inst_t *one = ir_inst_new(IR_LOAD_FP_IMM);
    if (!one) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    one->dst = ir_val_vreg(one_vreg, value_type);
    one->src1 = ir_val_fp_imm(value_type, 1.0);
    if (!hir_ir_append_instruction(context, one)) return ir_val_none();
    new_value = emit_float_binary(
        context, is_increment ? IR_FADD : IR_FSUB,
        old_value, one->dst, value_type);
  } else {
    new_value = hir_ir_emit_integer_binary(
        context, is_increment ? IR_ADD : IR_SUB, old_value,
        step_value.id == IR_VAL_NONE
            ? ir_val_imm(value_type, step) : step_value,
        value_type);
  }
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  if (is_bitfield) {
    (void)emit_bitfield_store(
        context, pointer, new_value, bit_width, bit_offset, value_type);
  } else {
    ir_inst_t *store = ir_inst_new(IR_STORE);
    if (!store) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    store->src1 = pointer;
    store->src2 = new_value;
    if (!hir_ir_append_instruction(context, store)) return ir_val_none();
  }
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  return kind == PSX_HIR_PRE_INC || kind == PSX_HIR_PRE_DEC
             ? new_value : old_value;
}

static int complex_compound_binary_kind(
    psx_hir_compound_operator_t compound_op,
    psx_hir_node_kind_t *kind) {
  if (!kind) return 0;
  switch (compound_op) {
    case PSX_HIR_COMPOUND_ADD: *kind = PSX_HIR_ADD; return 1;
    case PSX_HIR_COMPOUND_SUB: *kind = PSX_HIR_SUB; return 1;
    case PSX_HIR_COMPOUND_MUL: *kind = PSX_HIR_MUL; return 1;
    case PSX_HIR_COMPOUND_DIV: *kind = PSX_HIR_DIV; return 1;
    default: return 0;
  }
}

static int is_arithmetic_mir_type(ir_mir_type_info_t type) {
  return type.type_class == IR_MIR_TYPE_INTEGER ||
         hir_ir_is_float_value_type(type) ||
         hir_ir_is_complex_type(type);
}

static ir_mir_type_info_t complex_compound_operation_type(
    ir_mir_type_info_t target_type,
    ir_mir_type_info_t rhs_type) {
  ir_type_t component_type = IR_TY_F32;
  if (((hir_ir_is_float_value_type(target_type) ||
        hir_ir_is_complex_type(target_type)) &&
       target_type.type == IR_TY_F64) ||
      ((hir_ir_is_float_value_type(rhs_type) ||
        hir_ir_is_complex_type(rhs_type)) &&
       rhs_type.type == IR_TY_F64))
    component_type = IR_TY_F64;
  int component_size = ir_type_fixed_size(component_type);
  return (ir_mir_type_info_t){
      .type = component_type,
      .type_class = IR_MIR_TYPE_COMPLEX,
      .source_size = 2 * component_size,
  };
}

static ir_val_t complex_pointer_truth_value(
    hir_ir_context_t *context, ir_val_t value,
    ir_mir_type_info_t type) {
  if (value.type != IR_TY_PTR || !hir_ir_is_complex_type(type))
    return hir_ir_unsupported_expr(context);
  ir_val_t imaginary_pointer = hir_ir_pointer_with_offset(
      context, value, ir_type_fixed_size(type.type));
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_val_t real = load_direct_value(context, value, type.type);
  ir_val_t imaginary = load_direct_value(
      context, imaginary_pointer, type.type);
  if (context->status == IR_HIR_BUILD_OK)
    real = hir_ir_scalar_truth_value(context, real);
  if (context->status == IR_HIR_BUILD_OK)
    imaginary = hir_ir_scalar_truth_value(context, imaginary);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  return hir_ir_emit_integer_binary(
      context, IR_OR, real, imaginary, IR_TY_I32);
}

static ir_val_t build_complex_compound_assignment(
    hir_ir_context_t *context, const psx_hir_node_t *target,
    const psx_hir_node_t *rhs_node,
    ir_mir_type_info_t target_type,
    ir_mir_type_info_t rhs_type,
    psx_hir_compound_operator_t compound_op) {
  psx_hir_node_kind_t binary_kind = PSX_HIR_ADD;
  if (!target || !rhs_node || !hir_ir_node_is_lvalue(target) ||
      !is_arithmetic_mir_type(target_type) ||
      !is_arithmetic_mir_type(rhs_type) ||
      !complex_compound_binary_kind(compound_op, &binary_kind))
    return hir_ir_unsupported_expr(context);

  ir_val_t pointer = hir_ir_lvalue_address(context, target);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  int bit_width = 0;
  int bit_offset = 0;
  int bit_is_signed = 0;
  int is_bitfield = psx_hir_node_bitfield_info(
      target, &bit_width, &bit_offset, &bit_is_signed);
  if (hir_ir_is_complex_type(target_type) && is_bitfield)
    return hir_ir_unsupported_expr(context);

  ir_mir_type_info_t operation_type =
      complex_compound_operation_type(target_type, rhs_type);
  ir_val_t left;
  if (hir_ir_is_complex_type(target_type)) {
    left = convert_complex_pointer(
        context, pointer, target_type, operation_type);
  } else {
    ir_type_t target_storage_type =
        hir_ir_scalar_storage_type(target_type);
    ir_val_t old_value;
    if (is_bitfield) {
      old_value = emit_bitfield_load(
          context, pointer, bit_width, bit_offset, bit_is_signed,
          target_storage_type, target_storage_type);
    } else {
      old_value = load_direct_value(
          context, pointer, target_storage_type);
    }
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    left = materialize_direct_value_as_complex(
        context, old_value, target_type, operation_type);
  }
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();

  ir_val_t right = hir_ir_materialize_complex_operand(
      context, rhs_node, operation_type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_val_t result = emit_complex_binary_values(
      context, binary_kind, left, right, operation_type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();

  if (hir_ir_is_complex_type(target_type)) {
    result = convert_complex_pointer(
        context, result, operation_type, target_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    ir_inst_t *copy = ir_inst_new(IR_MEMCPY);
    if (!copy) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    copy->src1 = pointer;
    copy->src2 = result;
    copy->alloca_size = target_type.source_size;
    if (!hir_ir_append_instruction(context, copy)) return ir_val_none();
    return pointer;
  }

  psx_type_shape_t target_semantic_type = {0};
  int target_is_bool =
      hir_ir_node_type_shape(context, target, &target_semantic_type) &&
      target_semantic_type.kind == PSX_TYPE_BOOL;
  if (target_is_bool) {
    result = complex_pointer_truth_value(
        context, result, operation_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    result = hir_ir_coerce_direct_value(
        context, result,
        (ir_mir_type_info_t){
            .type = IR_TY_I32,
            .type_class = IR_MIR_TYPE_INTEGER,
            .source_size = 4,
            .is_unsigned = 1,
        },
        target_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  } else {
    ir_mir_type_info_t component_type = {
        .type = operation_type.type,
        .type_class = IR_MIR_TYPE_FLOAT,
        .source_size = ir_type_fixed_size(operation_type.type),
    };
    result = load_direct_value(context, result, operation_type.type);
    if (context->status == IR_HIR_BUILD_OK)
      result = hir_ir_coerce_direct_value(
          context, result, component_type, target_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  }
  if (is_bitfield)
    return emit_bitfield_store(
        context, pointer, result, bit_width, bit_offset,
        hir_ir_scalar_storage_type(target_type));
  if (!hir_ir_store_direct_value(context, pointer, result))
    return ir_val_none();
  return result;
}

static ir_val_t build_compound_assignment(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t target_type) {
  const psx_hir_node_t *target = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *rhs_node = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  if (!target || !rhs_node || !hir_ir_node_is_lvalue(target))
    return hir_ir_unsupported_expr(context);

  ir_mir_type_info_t rhs_type = hir_ir_classify_node_type(context, rhs_node);
  psx_hir_compound_operator_t compound_op =
      psx_hir_node_compound_operator(node);
  if (hir_ir_is_complex_type(target_type) ||
      hir_ir_is_complex_type(rhs_type))
    return build_complex_compound_assignment(
        context, target, rhs_node, target_type, rhs_type,
        compound_op);
  if (!hir_ir_is_scalar_value_type(target_type))
    return hir_ir_unsupported_expr(context);

  ir_val_t pointer = hir_ir_lvalue_address(context, target);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  int bit_width = 0;
  int bit_offset = 0;
  int bit_is_signed = 0;
  int is_bitfield = psx_hir_node_bitfield_info(
      target, &bit_width, &bit_offset, &bit_is_signed);
  ir_type_t target_storage_type = hir_ir_scalar_storage_type(target_type);
  ir_val_t old_value;
  if (is_bitfield) {
    old_value = emit_bitfield_load(
        context, pointer, bit_width, bit_offset, bit_is_signed,
        target_storage_type, target_storage_type);
  } else {
    int old_vreg = hir_ir_new_vreg(context);
    if (old_vreg < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(old_vreg, target_storage_type);
    load->src1 = pointer;
    load->is_unsigned = target_type.is_unsigned ? 1 : 0;
    if (!hir_ir_append_instruction(context, load)) return ir_val_none();
    old_value = load->dst;
  }
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();

  ir_val_t rhs = hir_ir_build_expr(context, rhs_node);
  if (context->status != IR_HIR_BUILD_OK ||
      !hir_ir_is_scalar_value_type(rhs_type))
    return hir_ir_unsupported_expr(context);

  ir_val_t result;
  ir_mir_type_info_t operation_type = target_type;
  if (target_type.type_class == IR_MIR_TYPE_POINTER) {
    if ((compound_op != PSX_HIR_COMPOUND_ADD &&
         compound_op != PSX_HIR_COMPOUND_SUB) ||
        rhs_type.type_class != IR_MIR_TYPE_INTEGER ||
        !hir_ir_is_integer_type(rhs.type))
      return hir_ir_unsupported_expr(context);
    rhs = hir_ir_emit_integer_width_conversion(
        context, rhs, IR_TY_I64, !rhs_type.is_unsigned);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    ir_val_t stride = ir_val_none();
    int stride_offset = psx_hir_node_vla_stride_frame_offset(target);
    if (stride_offset != 0) {
      int slot_size = psx_hir_node_vla_stride_slot_size(target);
      if (slot_size < 8) return hir_ir_unsupported_expr(context);
      int stride_pointer = hir_ir_local_storage_address(
          context, stride_offset, slot_size, 8);
      if (stride_pointer < 0) return ir_val_none();
      int stride_vreg = hir_ir_new_vreg(context);
      if (stride_vreg < 0) return ir_val_none();
      ir_inst_t *load = ir_inst_new(IR_LOAD);
      if (!load) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return ir_val_none();
      }
      load->dst = ir_val_vreg(stride_vreg, IR_TY_I64);
      load->src1 = ir_val_vreg(stride_pointer, IR_TY_PTR);
      if (!hir_ir_append_instruction(context, load)) return ir_val_none();
      stride = load->dst;
    } else {
      psx_qual_type_t pointee = psx_semantic_type_table_base(
          context->options->semantic_types,
          psx_hir_node_qual_type(target).type_id);
      int stride_bytes = psx_type_layout_sizeof(
          context->options->semantic_types, context->options->record_layouts,
          pointee.type_id,
          ag_target_info_data_layout(context->options->target));
      if (stride_bytes <= 0) return hir_ir_unsupported_expr(context);
      stride = ir_val_imm(IR_TY_I64, stride_bytes);
    }
    ir_val_t byte_offset = hir_ir_emit_integer_binary(
        context, IR_MUL, rhs, stride, IR_TY_I64);
    result = hir_ir_emit_integer_binary(
        context,
        compound_op == PSX_HIR_COMPOUND_ADD ? IR_ADD : IR_SUB,
        old_value, byte_offset, IR_TY_PTR);
  } else if (hir_ir_is_float_value_type(target_type) ||
             hir_ir_is_float_value_type(rhs_type)) {
    if (compound_op != PSX_HIR_COMPOUND_ADD &&
        compound_op != PSX_HIR_COMPOUND_SUB &&
        compound_op != PSX_HIR_COMPOUND_MUL &&
        compound_op != PSX_HIR_COMPOUND_DIV)
      return hir_ir_unsupported_expr(context);
    operation_type =
        hir_ir_is_float_value_type(rhs_type) &&
                (!hir_ir_is_float_value_type(target_type) ||
                 rhs_type.source_size > target_type.source_size)
            ? rhs_type : target_type;
    old_value = hir_ir_coerce_direct_value(
        context, old_value, target_type, operation_type);
    rhs = hir_ir_coerce_direct_value(
        context, rhs, rhs_type, operation_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    ir_op_t op = compound_op == PSX_HIR_COMPOUND_ADD ? IR_FADD
                 : compound_op == PSX_HIR_COMPOUND_SUB ? IR_FSUB
                 : compound_op == PSX_HIR_COMPOUND_MUL ? IR_FMUL
                                                       : IR_FDIV;
    result = emit_float_binary(
        context, op, old_value, rhs, operation_type.type);
  } else {
    if (target_type.type_class != IR_MIR_TYPE_INTEGER ||
        rhs_type.type_class != IR_MIR_TYPE_INTEGER)
      return hir_ir_unsupported_expr(context);
    int operation_size = target_type.source_size;
    if (operation_size < 4) operation_size = 4;
    if (compound_op != PSX_HIR_COMPOUND_SHL &&
        compound_op != PSX_HIR_COMPOUND_SHR &&
        rhs_type.source_size > operation_size)
      operation_size = rhs_type.source_size;
    operation_type = (ir_mir_type_info_t){
        .type = operation_size >= 8 ? IR_TY_I64 : IR_TY_I32,
        .type_class = IR_MIR_TYPE_INTEGER,
        .source_size = operation_size >= 8 ? 8 : 4,
    };
    operation_type.is_unsigned =
        compound_op == PSX_HIR_COMPOUND_SHL ||
                compound_op == PSX_HIR_COMPOUND_SHR
            ? ir_mir_integer_promotion_is_unsigned(
                  target_type,
                  ag_target_info_data_layout(context->options->target))
            : ir_mir_usual_arithmetic_result_is_unsigned(
                  target_type, rhs_type,
                  ag_target_info_data_layout(context->options->target));
    old_value = hir_ir_coerce_direct_value(
        context, old_value, target_type, operation_type);
    rhs = hir_ir_coerce_direct_value(
        context, rhs, rhs_type, operation_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    ir_op_t op;
    switch (compound_op) {
      case PSX_HIR_COMPOUND_ADD: op = IR_ADD; break;
      case PSX_HIR_COMPOUND_SUB: op = IR_SUB; break;
      case PSX_HIR_COMPOUND_MUL: op = IR_MUL; break;
      case PSX_HIR_COMPOUND_DIV:
        op = operation_type.is_unsigned ? IR_UDIV : IR_DIV;
        break;
      case PSX_HIR_COMPOUND_MOD:
        op = operation_type.is_unsigned ? IR_UMOD : IR_MOD;
        break;
      case PSX_HIR_COMPOUND_SHL: op = IR_SHL; break;
      case PSX_HIR_COMPOUND_SHR:
        op = operation_type.is_unsigned ? IR_LSR : IR_SHR;
        break;
      case PSX_HIR_COMPOUND_BITAND: op = IR_AND; break;
      case PSX_HIR_COMPOUND_BITXOR: op = IR_XOR; break;
      case PSX_HIR_COMPOUND_BITOR: op = IR_OR; break;
      default: return hir_ir_unsupported_expr(context);
    }
    result = hir_ir_emit_integer_binary(
        context, op, old_value, rhs, operation_type.type);
  }
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  result = hir_ir_coerce_direct_value(
      context, result, operation_type, target_type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  psx_type_shape_t target_semantic_type = {0};
  if (hir_ir_node_type_shape(context, target, &target_semantic_type) &&
      target_semantic_type.kind == PSX_TYPE_BOOL) {
    result = hir_ir_scalar_truth_value(context, result);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    result = hir_ir_coerce_direct_value(
        context, result,
        (ir_mir_type_info_t){
            .type = IR_TY_I32,
            .type_class = IR_MIR_TYPE_INTEGER,
            .source_size = 4,
            .is_unsigned = 1,
        },
        target_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  }
  if (is_bitfield)
    return emit_bitfield_store(
        context, pointer, result, bit_width, bit_offset,
        target_storage_type);
  if (!hir_ir_store_direct_value(context, pointer, result))
    return ir_val_none();
  return result;
}

static ir_val_t build_short_circuit(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    int is_and) {
  const psx_hir_node_t *left = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *right = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  if (!left || !right) return hir_ir_unsupported_expr(context);
  int slot = hir_ir_allocate_scalar_temp(context, 4, 4);
  if (slot < 0 ||
      !store_scalar_temp(
          context, slot, ir_val_imm(IR_TY_I32, is_and ? 0 : 1)))
    return ir_val_none();
  ir_val_t left_value = hir_ir_build_expr(context, left);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_block_t *right_block = hir_ir_cfg_new_block(context);
  ir_block_t *merge_block = hir_ir_cfg_new_block(context);
  if (!right_block || !merge_block ||
      !hir_ir_emit_conditional_branch(
          context, left_value,
          is_and ? right_block : merge_block,
          is_and ? merge_block : right_block) ||
      !hir_ir_cfg_switch_to_block(context, right_block))
    return ir_val_none();
  ir_val_t right_value = hir_ir_build_expr(context, right);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  right_value = hir_ir_scalar_truth_value(context, right_value);
  if (context->status != IR_HIR_BUILD_OK ||
      !store_scalar_temp(context, slot, right_value) ||
      !hir_ir_cfg_emit_branch(context, merge_block) ||
      !hir_ir_cfg_switch_to_block(context, merge_block))
    return ir_val_none();
  return load_scalar_temp(context, slot, IR_TY_I32, 0);
}

static ir_val_t build_scalar_ternary(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t result_type) {
  const psx_hir_node_t *condition = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *if_true = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  const psx_hir_node_t *if_false = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_ELSE, 0);
  if (!condition || !if_true || !if_false ||
      !hir_ir_is_direct_value_type(result_type))
    return hir_ir_unsupported_expr(context);
  int size = result_type.source_size;
  int alignment = size >= 8 ? 8 : size >= 4 ? 4 :
                  size >= 2 ? 2 : 1;
  int slot = hir_ir_allocate_scalar_temp(context, size, alignment);
  if (slot < 0) return ir_val_none();
  ir_val_t condition_value = hir_ir_build_expr(context, condition);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_block_t *true_block = hir_ir_cfg_new_block(context);
  ir_block_t *false_block = hir_ir_cfg_new_block(context);
  ir_block_t *merge_block = hir_ir_cfg_new_block(context);
  if (!true_block || !false_block || !merge_block ||
      !hir_ir_emit_conditional_branch(
          context, condition_value, true_block, false_block) ||
      !hir_ir_cfg_switch_to_block(context, true_block))
    return ir_val_none();
  ir_val_t true_value = hir_ir_build_expr(context, if_true);
  if (context->status == IR_HIR_BUILD_OK)
    true_value = hir_ir_coerce_direct_value(
        context, true_value, hir_ir_classify_node_type(context, if_true),
        result_type);
  if (context->status != IR_HIR_BUILD_OK ||
      !store_scalar_temp(context, slot, true_value) ||
      !hir_ir_cfg_emit_branch(context, merge_block) ||
      !hir_ir_cfg_switch_to_block(context, false_block))
    return ir_val_none();
  ir_val_t false_value = hir_ir_build_expr(context, if_false);
  if (context->status == IR_HIR_BUILD_OK)
    false_value = hir_ir_coerce_direct_value(
        context, false_value, hir_ir_classify_node_type(context, if_false),
        result_type);
  if (context->status != IR_HIR_BUILD_OK ||
      !store_scalar_temp(context, slot, false_value) ||
      !hir_ir_cfg_emit_branch(context, merge_block) ||
      !hir_ir_cfg_switch_to_block(context, merge_block))
    return ir_val_none();
  return load_scalar_temp(
      context, slot, hir_ir_scalar_storage_type(result_type),
      result_type.is_unsigned);
}

static ir_val_t build_void_ternary(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  const psx_hir_node_t *condition = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *if_true = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  const psx_hir_node_t *if_false = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_ELSE, 0);
  if (!condition || !if_true || !if_false)
    return hir_ir_unsupported_expr(context);
  ir_val_t condition_value = hir_ir_build_expr(context, condition);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_block_t *true_block = hir_ir_cfg_new_block(context);
  ir_block_t *false_block = hir_ir_cfg_new_block(context);
  ir_block_t *merge_block = hir_ir_cfg_new_block(context);
  if (!true_block || !false_block || !merge_block ||
      !hir_ir_emit_conditional_branch(
          context, condition_value, true_block, false_block) ||
      !hir_ir_cfg_switch_to_block(context, true_block))
    return ir_val_none();
  (void)hir_ir_build_expr(context, if_true);
  if (context->status != IR_HIR_BUILD_OK ||
      !hir_ir_cfg_emit_branch(context, merge_block) ||
      !hir_ir_cfg_switch_to_block(context, false_block))
    return ir_val_none();
  (void)hir_ir_build_expr(context, if_false);
  if (context->status != IR_HIR_BUILD_OK ||
      !hir_ir_cfg_emit_branch(context, merge_block) ||
      !hir_ir_cfg_switch_to_block(context, merge_block))
    return ir_val_none();
  return ir_val_none();
}


static ir_val_t build_string_reference(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  size_t name_length = 0;
  const char *name = psx_hir_node_name(node, &name_length);
  int object_size = psx_hir_node_object_size(node);
  if (!name || name_length == 0 || object_size <= 0)
    return hir_ir_unsupported_expr(context);
  int result_vreg = hir_ir_new_vreg(context);
  if (result_vreg < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_LOAD_STR);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(result_vreg, IR_TY_PTR);
  load->sym = (char *)name;
  load->sym_len = (int)name_length;
  load->object_size = object_size;
  if (!hir_ir_append_instruction(context, load)) return ir_val_none();
  return load->dst;
}

static ir_val_t build_function_reference(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  size_t name_length = 0;
  const char *name = psx_hir_node_name(node, &name_length);
  if (!name || name_length == 0)
    return hir_ir_unsupported_expr(context);
  int result_vreg = hir_ir_new_vreg(context);
  if (result_vreg < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_LOAD_SYM);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(result_vreg, IR_TY_PTR);
  load->sym = (char *)name;
  load->sym_len = (int)name_length;
  load->is_function_symbol = 1;
  if (!ir_function_type_from_type_id(
          context->options->semantic_types,
          psx_hir_node_qual_type(node).type_id,
          &load->function_type)) {
    free(load);
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->has_function_type = 1;
  if (!hir_ir_append_instruction(context, load)) return ir_val_none();
  return load->dst;
}

ir_val_t hir_ir_build_expr(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  if (!node || context->status != IR_HIR_BUILD_OK)
    return ir_val_none();
  if (psx_hir_node_kind(node) == PSX_HIR_VLA_ALLOC)
    return hir_ir_build_vla_allocation(context, node);
  if (psx_hir_node_kind(node) == PSX_HIR_OBJECT_COPY)
    return hir_ir_build_object_copy(context, node);
  if (psx_hir_node_kind(node) == PSX_HIR_STMT_EXPR) {
    const psx_hir_node_t *prefix = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    const psx_hir_node_t *value = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_RHS, 0);
    if (!prefix || psx_hir_node_kind(prefix) != PSX_HIR_BLOCK ||
        !value || !hir_ir_build_statement(context, prefix))
      return hir_ir_unsupported_expr(context);
    return hir_ir_build_expr(context, value);
  }
  ir_mir_type_info_t type = hir_ir_classify_node_type(context, node);
  int is_void = hir_ir_node_type_kind(context, node) == PSX_TYPE_VOID;
  if (psx_hir_node_kind(node) == PSX_HIR_CALL && is_void)
    return hir_ir_build_call(context, node, type);
  if (psx_hir_node_kind(node) == PSX_HIR_CAST && is_void) {
    const psx_hir_node_t *operand = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    if (!operand) return hir_ir_unsupported_expr(context);
    (void)hir_ir_build_expr(context, operand);
    return ir_val_none();
  }
  if (psx_hir_node_kind(node) == PSX_HIR_COMMA && is_void) {
    const psx_hir_node_t *left = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    const psx_hir_node_t *right = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_RHS, 0);
    if (!left || !right) return hir_ir_unsupported_expr(context);
    (void)hir_ir_build_expr(context, left);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    (void)hir_ir_build_expr(context, right);
    return ir_val_none();
  }
  if (psx_hir_node_kind(node) == PSX_HIR_TERNARY && is_void)
    return build_void_ternary(context, node);
  if (type.type_class == IR_MIR_TYPE_AGGREGATE) {
    psx_hir_node_kind_t kind = psx_hir_node_kind(node);
    if (hir_ir_node_is_lvalue(node) || kind == PSX_HIR_CALL ||
        kind == PSX_HIR_CAST || kind == PSX_HIR_ASSIGN ||
        kind == PSX_HIR_COMMA ||
        kind == PSX_HIR_TERNARY)
      return hir_ir_aggregate_value_address(context, node);
    return hir_ir_unsupported_expr(context);
  }
  if (hir_ir_is_complex_type(type)) {
    psx_hir_node_kind_t kind = psx_hir_node_kind(node);
    if (hir_ir_node_is_lvalue(node))
      return hir_ir_lvalue_address(context, node);
    if (kind == PSX_HIR_CALL)
      return hir_ir_build_call(context, node, type);
    if (kind == PSX_HIR_CAST) {
      const psx_hir_node_t *operand = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_LHS, 0);
      if (!operand) return hir_ir_unsupported_expr(context);
      return hir_ir_materialize_complex_operand(context, operand, type);
    }
    if (kind == PSX_HIR_UNARY_PLUS)
      return build_unary_plus(context, node, type);
    if (kind == PSX_HIR_ASSIGN)
      return build_complex_assignment(context, node, type);
    if (kind == PSX_HIR_COMPOUND_ASSIGN)
      return build_compound_assignment(context, node, type);
    if (kind == PSX_HIR_NEGATE)
      return build_complex_negate(context, node, type);
    if (kind == PSX_HIR_ADD || kind == PSX_HIR_SUB ||
        kind == PSX_HIR_MUL || kind == PSX_HIR_DIV)
      return build_complex_binary(context, node, type);
    if (kind == PSX_HIR_COMMA) {
      const psx_hir_node_t *left = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_LHS, 0);
      const psx_hir_node_t *right = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_RHS, 0);
      if (!left || !right) return hir_ir_unsupported_expr(context);
      (void)hir_ir_build_expr(context, left);
      if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
      return hir_ir_build_expr(context, right);
    }
    return hir_ir_unsupported_expr(context);
  }
  if (!hir_ir_is_direct_value_type(type)) {
    return hir_ir_unsupported_expr(context);
  }
  if (psx_hir_node_kind(node) == PSX_HIR_CREAL ||
      psx_hir_node_kind(node) == PSX_HIR_CIMAG)
    return build_complex_component(context, node, type);
  if (psx_hir_node_kind(node) == PSX_HIR_NUMBER) {
    if (hir_ir_is_float_value_type(type)) {
      int result = hir_ir_new_vreg(context);
      if (result < 0) return ir_val_none();
      ir_inst_t *load = ir_inst_new(IR_LOAD_FP_IMM);
      if (!load) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return ir_val_none();
      }
      load->dst = ir_val_vreg(result, type.type);
      load->src1 = ir_val_fp_imm(
          type.type, psx_hir_node_floating_value(node));
      if (!hir_ir_append_instruction(context, load)) return ir_val_none();
      return load->dst;
    }
    if (type.type_class != IR_MIR_TYPE_INTEGER)
      return hir_ir_unsupported_expr(context);
    return ir_val_imm(type.type, psx_hir_node_integer_value(node));
  }
  if (psx_hir_node_kind(node) == PSX_HIR_STRING)
    return build_string_reference(context, node);
  if (psx_hir_node_kind(node) == PSX_HIR_FUNCTION_REF)
    return build_function_reference(context, node);
  if (psx_hir_node_kind(node) == PSX_HIR_VARARG_CURSOR) {
    if (type.type_class != IR_MIR_TYPE_POINTER)
      return hir_ir_unsupported_expr(context);
    int result = hir_ir_new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *area = ir_inst_new(IR_VARARG_CURSOR);
    if (!area) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    area->dst = ir_val_vreg(result, IR_TY_PTR);
    if (!hir_ir_append_instruction(context, area)) return ir_val_none();
    return area->dst;
  }
  if (psx_hir_node_kind(node) == PSX_HIR_LOGAND)
    return build_short_circuit(context, node, 1);
  if (psx_hir_node_kind(node) == PSX_HIR_LOGOR)
    return build_short_circuit(context, node, 0);
  if (psx_hir_node_kind(node) == PSX_HIR_LOGICAL_NOT)
    return build_logical_not(context, node, type);
  if (psx_hir_node_kind(node) == PSX_HIR_TERNARY)
    return build_scalar_ternary(context, node, type);
  if (psx_hir_node_kind(node) == PSX_HIR_PRE_INC ||
      psx_hir_node_kind(node) == PSX_HIR_PRE_DEC ||
      psx_hir_node_kind(node) == PSX_HIR_POST_INC ||
      psx_hir_node_kind(node) == PSX_HIR_POST_DEC)
    return build_inc_dec(context, node, type);
  if (psx_hir_node_kind(node) == PSX_HIR_COMPOUND_ASSIGN)
    return build_compound_assignment(context, node, type);

  if (psx_hir_node_kind(node) == PSX_HIR_GLOBAL) {
    ir_val_t pointer = global_address(context, node);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    psx_type_kind_t global_kind = hir_ir_node_type_kind(context, node);
    if (global_kind == PSX_TYPE_ARRAY ||
        global_kind == PSX_TYPE_FUNCTION)
      return pointer;
    int bit_width = 0;
    int bit_offset = 0;
    int bit_is_signed = 0;
    if (psx_hir_node_bitfield_info(
            node, &bit_width, &bit_offset, &bit_is_signed))
      return emit_bitfield_load(
          context, pointer, bit_width, bit_offset, bit_is_signed,
          hir_ir_scalar_storage_type(type),
          hir_ir_scalar_storage_type(type));
    int result = hir_ir_new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(result, hir_ir_scalar_storage_type(type));
    load->src1 = pointer;
    load->is_unsigned = type.is_unsigned ? 1 : 0;
    if (!hir_ir_append_instruction(context, load)) return ir_val_none();
    return load->dst;
  }

  if (psx_hir_node_kind(node) == PSX_HIR_ADDRESS) {
    const psx_hir_node_t *operand = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    if (type.type_class != IR_MIR_TYPE_POINTER || !operand)
      return hir_ir_unsupported_expr(context);
    return hir_ir_lvalue_address(context, operand);
  }

  if (psx_hir_node_kind(node) == PSX_HIR_DEREF) {
    ir_val_t pointer = hir_ir_lvalue_address(context, node);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    psx_type_kind_t dereferenced_kind = hir_ir_node_type_kind(context, node);
    if (dereferenced_kind == PSX_TYPE_ARRAY ||
        dereferenced_kind == PSX_TYPE_FUNCTION)
      return pointer;
    int bit_width = 0;
    int bit_offset = 0;
    int bit_is_signed = 0;
    if (psx_hir_node_bitfield_info(
            node, &bit_width, &bit_offset, &bit_is_signed))
      return emit_bitfield_load(
          context, pointer, bit_width, bit_offset, bit_is_signed,
          hir_ir_scalar_storage_type(type),
          hir_ir_scalar_storage_type(type));
    int result = hir_ir_new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(result, hir_ir_scalar_storage_type(type));
    load->src1 = pointer;
    load->is_unsigned = type.is_unsigned ? 1 : 0;
    if (!hir_ir_append_instruction(context, load)) return ir_val_none();
    return load->dst;
  }

  if (psx_hir_node_kind(node) == PSX_HIR_SUBSCRIPT) {
    ir_val_t pointer = hir_ir_lvalue_address(context, node);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    psx_type_kind_t result_kind = hir_ir_node_type_kind(context, node);
    if (result_kind == PSX_TYPE_ARRAY ||
        result_kind == PSX_TYPE_FUNCTION)
      return pointer;
    int result = hir_ir_new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(result, hir_ir_scalar_storage_type(type));
    load->src1 = pointer;
    load->is_unsigned = type.is_unsigned ? 1 : 0;
    if (!hir_ir_append_instruction(context, load)) return ir_val_none();
    return load->dst;
  }

  if (psx_hir_node_kind(node) == PSX_HIR_MEMBER_ACCESS) {
    ir_val_t pointer = hir_ir_lvalue_address(context, node);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    psx_type_kind_t result_kind = hir_ir_node_type_kind(context, node);
    if (result_kind == PSX_TYPE_ARRAY ||
        result_kind == PSX_TYPE_FUNCTION)
      return pointer;
    int bit_width = 0;
    int bit_offset = 0;
    int bit_is_signed = 0;
    if (psx_hir_node_bitfield_info(
            node, &bit_width, &bit_offset, &bit_is_signed))
      return emit_bitfield_load(
          context, pointer, bit_width, bit_offset, bit_is_signed,
          hir_ir_scalar_storage_type(type),
          hir_ir_scalar_storage_type(type));
    int result = hir_ir_new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(result, hir_ir_scalar_storage_type(type));
    load->src1 = pointer;
    load->is_unsigned = type.is_unsigned ? 1 : 0;
    if (!hir_ir_append_instruction(context, load)) return ir_val_none();
    return load->dst;
  }

  if (psx_hir_node_kind(node) == PSX_HIR_LOCAL) {
    int pointer = hir_ir_local_address(context, node);
    if (pointer < 0) return ir_val_none();
    psx_type_kind_t local_kind = hir_ir_node_type_kind(context, node);
    if (local_kind == PSX_TYPE_ARRAY ||
        local_kind == PSX_TYPE_FUNCTION)
      return ir_val_vreg(pointer, IR_TY_PTR);
    int bit_width = 0;
    int bit_offset = 0;
    int bit_is_signed = 0;
    if (psx_hir_node_bitfield_info(
            node, &bit_width, &bit_offset, &bit_is_signed))
      return emit_bitfield_load(
          context, ir_val_vreg(pointer, IR_TY_PTR), bit_width,
          bit_offset, bit_is_signed, hir_ir_scalar_storage_type(type),
          hir_ir_scalar_storage_type(type));
    int result = hir_ir_new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(result, hir_ir_scalar_storage_type(type));
    load->src1 = ir_val_vreg(pointer, IR_TY_PTR);
    load->is_unsigned = type.is_unsigned ? 1 : 0;
    if (!hir_ir_append_instruction(context, load)) return ir_val_none();
    return load->dst;
  }

  if (psx_hir_node_kind(node) == PSX_HIR_CAST ||
      psx_hir_node_kind(node) == PSX_HIR_FP_TO_INT ||
      psx_hir_node_kind(node) == PSX_HIR_INT_TO_FP) {
    const psx_hir_node_t *operand = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    if (!operand) return hir_ir_unsupported_expr(context);
    ir_val_t value = hir_ir_build_expr(context, operand);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    ir_mir_type_info_t source_type =
        hir_ir_classify_node_type(context, operand);
    return psx_hir_node_kind(node) == PSX_HIR_CAST
               ? coerce_explicit_cast_value(
                     context, value, source_type, type,
                     psx_hir_node_qual_type(node))
               : hir_ir_coerce_direct_value(
                     context, value, source_type, type);
  }

  if (psx_hir_node_kind(node) == PSX_HIR_UNARY_PLUS)
    return build_unary_plus(context, node, type);
  if (psx_hir_node_kind(node) == PSX_HIR_NEGATE)
    return build_scalar_negate(context, node, type);
  if (psx_hir_node_kind(node) == PSX_HIR_BITWISE_NOT)
    return build_bitwise_not(context, node, type);

  if (psx_hir_node_kind(node) == PSX_HIR_COMMA) {
    const psx_hir_node_t *left = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    const psx_hir_node_t *right = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_RHS, 0);
    if (!left || !right) return hir_ir_unsupported_expr(context);
    (void)hir_ir_build_expr(context, left);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    return hir_ir_build_expr(context, right);
  }

  if (psx_hir_node_kind(node) == PSX_HIR_CALL)
    return hir_ir_build_call(context, node, type);

  if (psx_hir_node_kind(node) == PSX_HIR_ASSIGN) {
    const psx_hir_node_t *target = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    const psx_hir_node_t *value_node = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_RHS, 0);
    if (!target || !value_node || !hir_ir_node_is_lvalue(target))
      return hir_ir_unsupported_expr(context);
    ir_val_t value = hir_ir_build_expr(context, value_node);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    ir_mir_type_info_t target_type = hir_ir_classify_node_type(context, target);
    value = hir_ir_coerce_direct_value_to_qual_type(
        context, value, hir_ir_classify_node_type(context, value_node),
        target_type, psx_hir_node_qual_type(target));
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    ir_val_t pointer = hir_ir_lvalue_address(context, target);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    int bit_width = 0;
    int bit_offset = 0;
    int bit_is_signed = 0;
    if (psx_hir_node_bitfield_info(
            target, &bit_width, &bit_offset, &bit_is_signed)) {
      (void)bit_is_signed;
      return emit_bitfield_store(
          context, pointer, value, bit_width, bit_offset,
          hir_ir_scalar_storage_type(target_type));
    }
    ir_inst_t *store = ir_inst_new(IR_STORE);
    if (!store) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    store->src1 = pointer;
    store->src2 = value;
    if (!hir_ir_append_instruction(context, store)) return ir_val_none();
    return value;
  }

  const psx_hir_node_t *lhs = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *rhs = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  if (!lhs || !rhs) return hir_ir_unsupported_expr(context);
  psx_hir_node_kind_t kind = psx_hir_node_kind(node);
  int reverse_comparison =
      kind == PSX_HIR_GT || kind == PSX_HIR_GE;
  ir_val_t pointer_result = ir_val_none();
  if (try_build_pointer_arithmetic(
          context, node, lhs, rhs, type, &pointer_result))
    return pointer_result;
  ir_val_t left = hir_ir_build_expr(context, lhs);
  ir_val_t right = hir_ir_build_expr(context, rhs);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();

  ir_mir_type_info_t left_type = hir_ir_classify_node_type(context, lhs);
  ir_mir_type_info_t right_type = hir_ir_classify_node_type(context, rhs);
  if (hir_ir_is_complex_type(left_type) || hir_ir_is_complex_type(right_type)) {
    if (!hir_ir_is_complex_type(left_type) ||
        !hir_ir_is_complex_type(right_type) ||
        left_type.type != right_type.type ||
        left_type.source_size != right_type.source_size)
      return hir_ir_unsupported_expr(context);
    return build_complex_comparison(
        context, kind, left, right, left_type);
  }
  int is_float = hir_ir_is_float_value_type(left_type) ||
                 hir_ir_is_float_value_type(right_type);
  if (is_float) {
    if (kind != PSX_HIR_ADD && kind != PSX_HIR_SUB &&
        kind != PSX_HIR_MUL && kind != PSX_HIR_DIV &&
        kind != PSX_HIR_EQ && kind != PSX_HIR_NE &&
        kind != PSX_HIR_LT && kind != PSX_HIR_LE &&
        kind != PSX_HIR_GT && kind != PSX_HIR_GE)
      return hir_ir_unsupported_expr(context);
    ir_type_t fp_type = left_type.type == IR_TY_F64 ||
                                right_type.type == IR_TY_F64
                            ? IR_TY_F64
                            : IR_TY_F32;
    ir_mir_type_info_t arithmetic_type = {
        .type = fp_type,
        .type_class = IR_MIR_TYPE_FLOAT,
        .source_size = ir_type_fixed_size(fp_type),
    };
    left = hir_ir_coerce_direct_value(
        context, left, left_type, arithmetic_type);
    if (context->status == IR_HIR_BUILD_OK)
      right = hir_ir_coerce_direct_value(
          context, right, right_type, arithmetic_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    ir_op_t fp_op;
    switch (kind) {
      case PSX_HIR_ADD: fp_op = IR_FADD; break;
      case PSX_HIR_SUB: fp_op = IR_FSUB; break;
      case PSX_HIR_MUL: fp_op = IR_FMUL; break;
      case PSX_HIR_DIV: fp_op = IR_FDIV; break;
      case PSX_HIR_EQ: fp_op = IR_FEQ; break;
      case PSX_HIR_NE: fp_op = IR_FNE; break;
      case PSX_HIR_LT: fp_op = IR_FLT; break;
      case PSX_HIR_LE: fp_op = IR_FLE; break;
      case PSX_HIR_GT: fp_op = IR_FLT; break;
      case PSX_HIR_GE: fp_op = IR_FLE; break;
      default: return hir_ir_unsupported_expr(context);
    }
    int result_vreg = hir_ir_new_vreg(context);
    if (result_vreg < 0) return ir_val_none();
    ir_inst_t *instruction = ir_inst_new(fp_op);
    if (!instruction) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    instruction->dst = ir_val_vreg(result_vreg, type.type);
    instruction->src1 = reverse_comparison ? right : left;
    instruction->src2 = reverse_comparison ? left : right;
    if (!hir_ir_append_instruction(context, instruction)) return ir_val_none();
    return instruction->dst;
  }
  int uac_is_unsigned = ir_mir_usual_arithmetic_result_is_unsigned(
      left_type, right_type,
      ag_target_info_data_layout(context->options->target));
  int shift_is_unsigned = ir_mir_integer_promotion_is_unsigned(
      left_type, ag_target_info_data_layout(context->options->target));
  ir_op_t op;
  switch (kind) {
    case PSX_HIR_ADD: op = IR_ADD; break;
    case PSX_HIR_SUB: op = IR_SUB; break;
    case PSX_HIR_MUL: op = IR_MUL; break;
    case PSX_HIR_DIV:
      if (left_type.type_class != IR_MIR_TYPE_INTEGER)
        return hir_ir_unsupported_expr(context);
      op = uac_is_unsigned ? IR_UDIV : IR_DIV;
      break;
    case PSX_HIR_MOD:
      if (left_type.type_class != IR_MIR_TYPE_INTEGER)
        return hir_ir_unsupported_expr(context);
      op = uac_is_unsigned ? IR_UMOD : IR_MOD;
      break;
    case PSX_HIR_BITAND: op = IR_AND; break;
    case PSX_HIR_BITOR: op = IR_OR; break;
    case PSX_HIR_BITXOR: op = IR_XOR; break;
    case PSX_HIR_SHL: op = IR_SHL; break;
    case PSX_HIR_SHR: op = shift_is_unsigned ? IR_LSR : IR_SHR; break;
    case PSX_HIR_EQ: op = IR_EQ; break;
    case PSX_HIR_NE: op = IR_NE; break;
    case PSX_HIR_LT:
      op = left_type.type_class == IR_MIR_TYPE_POINTER ||
                   uac_is_unsigned ? IR_ULT : IR_LT;
      break;
    case PSX_HIR_LE:
      op = left_type.type_class == IR_MIR_TYPE_POINTER ||
                   uac_is_unsigned ? IR_ULE : IR_LE;
      break;
    case PSX_HIR_GT:
      op = left_type.type_class == IR_MIR_TYPE_POINTER ||
                   uac_is_unsigned ? IR_ULT : IR_LT;
      break;
    case PSX_HIR_GE:
      op = left_type.type_class == IR_MIR_TYPE_POINTER ||
                   uac_is_unsigned ? IR_ULE : IR_LE;
      break;
    default: return hir_ir_unsupported_expr(context);
  }
  int result_vreg = hir_ir_new_vreg(context);
  if (result_vreg < 0) return ir_val_none();
  ir_inst_t *instruction = ir_inst_new(op);
  if (!instruction) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  instruction->dst = ir_val_vreg(result_vreg, type.type);
  instruction->src1 = reverse_comparison ? right : left;
  instruction->src2 = reverse_comparison ? left : right;
  if (!hir_ir_append_instruction(context, instruction)) return ir_val_none();
  return instruction->dst;
}
