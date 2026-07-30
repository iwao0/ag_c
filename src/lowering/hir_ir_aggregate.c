#include "hir_ir_builder_internal.h"
#include "../type_layout.h"

#include <stdlib.h>

int hir_ir_atomic_object_storage_width(
    const hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t type) {
  if (!context || !node ||
      (psx_hir_node_qual_type(node).qualifiers &
       PSX_TYPE_QUALIFIER_ATOMIC) == 0 ||
      (type.type_class != IR_MIR_TYPE_AGGREGATE &&
       type.type_class != IR_MIR_TYPE_COMPLEX))
    return 0;
  int width = psx_qual_type_layout_sizeof(
      context->options->semantic_types,
      context->options->record_layouts,
      psx_hir_node_qual_type(node),
      ag_target_info_data_layout(context->options->target));
  if (width == 1 || width == 2 || width == 4 || width == 8)
    return width;
  return width == 16 &&
                 ag_target_info_call_abi(context->options->target) ==
                     AG_TARGET_CALL_ABI_AAPCS64
             ? 16 : 0;
}

static ir_type_t atomic_aggregate_storage_type(int width) {
  switch (width) {
    case 1:
      return IR_TY_I8;
    case 2:
      return IR_TY_I16;
    case 4:
      return IR_TY_I32;
    case 8:
      return IR_TY_I64;
    default:
      return IR_TY_VOID;
  }
}

ir_val_t hir_ir_load_atomic_object_value(
    hir_ir_context_t *context, ir_val_t pointer, int width) {
  ir_type_t storage_type = atomic_aggregate_storage_type(width);
  if (pointer.type != IR_TY_PTR ||
      (storage_type == IR_TY_VOID && width != 16))
    return hir_ir_unsupported_expr(context);
  int alignment = width >= 16 ? 16 : width >= 8 ? 8 : width;
  int temporary =
      hir_ir_allocate_scalar_temp(context, width, alignment);
  if (temporary < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_ATOMIC);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->atomic_kind = IR_ATOMIC_LOAD;
  load->atomic_width = (unsigned char)width;
  load->is_unsigned = 1;
  load->src1 = pointer;
  if (width == 16) {
    load->src2 = ir_val_vreg(temporary, IR_TY_PTR);
    if (!hir_ir_append_instruction(context, load))
      return ir_val_none();
    return load->src2;
  }
  int result = hir_ir_new_vreg(context);
  if (result < 0) {
    free(load);
    return ir_val_none();
  }
  load->dst = ir_val_vreg(
      result, width == 8 ? IR_TY_I64 : IR_TY_I32);
  if (!hir_ir_append_instruction(context, load))
    return ir_val_none();
  ir_val_t bits = load->dst;
  if (bits.type != storage_type)
    bits = hir_ir_emit_integer_width_conversion(
        context, bits, storage_type, 0);
  if (context->status != IR_HIR_BUILD_OK ||
      !hir_ir_store_direct_value(
          context, ir_val_vreg(temporary, IR_TY_PTR), bits))
    return ir_val_none();
  return ir_val_vreg(temporary, IR_TY_PTR);
}

ir_val_t hir_ir_prepare_atomic_object_value(
    hir_ir_context_t *context, ir_val_t value,
    int value_size, int width) {
  ir_type_t storage_type = atomic_aggregate_storage_type(width);
  if (value.type != IR_TY_PTR || value_size <= 0 ||
      value_size > width ||
      (storage_type == IR_TY_VOID && width != 16))
    return ir_val_none();
  if (value_size < width || width == 16) {
    int temporary = hir_ir_allocate_scalar_temp(
        context, width, width >= 16 ? 16 : width >= 8 ? 8 : width);
    if (temporary < 0) return ir_val_none();
    ir_val_t temporary_pointer =
        ir_val_vreg(temporary, IR_TY_PTR);
    if (width == 16) {
      if (!hir_ir_store_direct_value(
              context, temporary_pointer,
              ir_val_imm(IR_TY_I64, 0)) ||
          !hir_ir_store_direct_value(
              context,
              hir_ir_pointer_with_offset(
                  context, temporary_pointer, 8),
              ir_val_imm(IR_TY_I64, 0)))
        return ir_val_none();
    } else if (!hir_ir_store_direct_value(
                   context, temporary_pointer,
                   ir_val_imm(storage_type, 0))) {
      return ir_val_none();
    }
    ir_inst_t *copy = ir_inst_new(IR_MEMCPY);
    if (!copy) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    copy->src1 = temporary_pointer;
    copy->src2 = value;
    copy->alloca_size = value_size;
    if (!hir_ir_append_instruction(context, copy))
      return ir_val_none();
    value = temporary_pointer;
  }
  return value;
}

int hir_ir_store_atomic_object_value(
    hir_ir_context_t *context, ir_val_t pointer, ir_val_t value,
    int value_size, int width) {
  if (pointer.type != IR_TY_PTR) return 0;
  value = hir_ir_prepare_atomic_object_value(
      context, value, value_size, width);
  if (context->status != IR_HIR_BUILD_OK ||
      value.type != IR_TY_PTR)
    return 0;
  ir_type_t storage_type =
      atomic_aggregate_storage_type(width);
  if (width == 16) {
    ir_inst_t *store = ir_inst_new(IR_ATOMIC);
    if (!store) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return 0;
    }
    store->atomic_kind = IR_ATOMIC_STORE;
    store->atomic_width = 16;
    store->src1 = pointer;
    store->src2 = value;
    return hir_ir_append_instruction(context, store);
  }
  int bits_vreg = hir_ir_new_vreg(context);
  if (bits_vreg < 0) return 0;
  ir_inst_t *load = ir_inst_new(IR_LOAD);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  load->dst = ir_val_vreg(bits_vreg, storage_type);
  load->src1 = value;
  load->is_unsigned = 1;
  if (!hir_ir_append_instruction(context, load)) return 0;
  ir_inst_t *store = ir_inst_new(IR_ATOMIC);
  if (!store) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  store->atomic_kind = IR_ATOMIC_STORE;
  store->atomic_width = (unsigned char)width;
  store->src1 = pointer;
  store->src2 = load->dst;
  return hir_ir_append_instruction(context, store);
}

ir_val_t hir_ir_build_aggregate_assignment(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t target_type) {
  const psx_hir_node_t *target = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *source_node = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  ir_mir_type_info_t source_type = hir_ir_classify_node_type(
      context, source_node);
  if (!target || !source_node ||
      target_type.type_class != IR_MIR_TYPE_AGGREGATE ||
      source_type.type_class != IR_MIR_TYPE_AGGREGATE ||
      target_type.source_size <= 0 ||
      source_type.source_size != target_type.source_size)
    return hir_ir_unsupported_expr(context);
  ir_val_t destination = hir_ir_lvalue_address(context, target);
  ir_val_t source = hir_ir_aggregate_value_address(context, source_node);
  if (context->status != IR_HIR_BUILD_OK ||
      destination.type != IR_TY_PTR || source.type != IR_TY_PTR)
    return ir_val_none();
  int atomic_width =
      hir_ir_atomic_object_storage_width(
          context, target, target_type);
  if (atomic_width != 0) {
    if (!hir_ir_store_atomic_object_value(
            context, destination, source,
            target_type.source_size, atomic_width))
      return hir_ir_unsupported_expr(context);
    return source;
  }
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

ir_val_t hir_ir_build_object_copy(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  const psx_hir_node_t *target = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *source_node = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  psx_type_shape_t target_semantic_type = {0};
  int has_target_semantic_type = target && hir_ir_node_type_shape(
      context, target, &target_semantic_type);
  int copy_size =
      target ? psx_type_layout_sizeof(
                   context->options->semantic_types,
                   context->options->record_layouts,
                   psx_hir_node_qual_type(target).type_id,
                   ag_target_info_data_layout(context->options->target))
             : 0;
  int is_array = has_target_semantic_type &&
      target_semantic_type.kind == PSX_TYPE_ARRAY;
  int is_record = has_target_semantic_type &&
      (target_semantic_type.kind == PSX_TYPE_STRUCT ||
       target_semantic_type.kind == PSX_TYPE_UNION);
  if (!target || !source_node || (!is_array && !is_record) ||
      copy_size <= 0 || !hir_ir_node_is_lvalue(target))
    return hir_ir_unsupported_expr(context);
  ir_val_t destination = hir_ir_lvalue_address(context, target);
  ir_val_t source = is_record
      ? hir_ir_aggregate_value_address(context, source_node)
      : hir_ir_build_expr(context, source_node);
  if (context->status != IR_HIR_BUILD_OK ||
      destination.type != IR_TY_PTR || source.type != IR_TY_PTR)
    return hir_ir_unsupported_expr(context);
  ir_inst_t *copy = ir_inst_new(IR_MEMCPY);
  if (!copy) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  copy->src1 = destination;
  copy->src2 = source;
  copy->alloca_size = copy_size;
  if (!hir_ir_append_instruction(context, copy)) return ir_val_none();
  return destination;
}

static int copy_aggregate_value_to(
    hir_ir_context_t *context, const psx_hir_node_t *source_node,
    ir_val_t destination, ir_mir_type_info_t result_type) {
  ir_mir_type_info_t source_type = hir_ir_classify_node_type(
      context, source_node);
  if (destination.type != IR_TY_PTR ||
      result_type.type_class != IR_MIR_TYPE_AGGREGATE ||
      source_type.type_class != IR_MIR_TYPE_AGGREGATE ||
      result_type.source_size <= 0 ||
      source_type.source_size != result_type.source_size) {
    hir_ir_unsupported_expr(context);
    return 0;
  }
  ir_val_t source = hir_ir_aggregate_value_address(context, source_node);
  if (context->status != IR_HIR_BUILD_OK || source.type != IR_TY_PTR)
    return 0;
  ir_inst_t *copy = ir_inst_new(IR_MEMCPY);
  if (!copy) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  copy->src1 = destination;
  copy->src2 = source;
  copy->alloca_size = result_type.source_size;
  return hir_ir_append_instruction(context, copy);
}

static ir_val_t materialize_volatile_aggregate_lvalue(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_val_t source, ir_mir_type_info_t type) {
  if (source.type != IR_TY_PTR ||
      type.type_class != IR_MIR_TYPE_AGGREGATE ||
      type.source_size <= 0)
    return hir_ir_unsupported_expr(context);
  int alignment = psx_qual_type_layout_alignof(
      context->options->semantic_types,
      context->options->record_layouts,
      psx_hir_node_qual_type(node),
      ag_target_info_data_layout(context->options->target));
  if (alignment > 16) alignment = 16;
  int temporary = hir_ir_allocate_scalar_temp(
      context, type.source_size, alignment);
  if (temporary < 0) return ir_val_none();
  ir_inst_t *copy = ir_inst_new(IR_MEMCPY);
  if (!copy) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  copy->src1 = ir_val_vreg(temporary, IR_TY_PTR);
  copy->src2 = source;
  copy->alloca_size = type.source_size;
  if (!hir_ir_append_instruction(context, copy))
    return ir_val_none();
  return copy->src1;
}

static ir_val_t build_aggregate_ternary_address(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t result_type) {
  const psx_hir_node_t *condition = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *if_true = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  const psx_hir_node_t *if_false = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_ELSE, 0);
  if (!condition || !if_true || !if_false ||
      result_type.type_class != IR_MIR_TYPE_AGGREGATE ||
      result_type.source_size <= 0)
    return hir_ir_unsupported_expr(context);
  int temporary = hir_ir_allocate_scalar_temp(
      context, result_type.source_size,
      result_type.source_size >= 8 ? 8 : 4);
  if (temporary < 0) return ir_val_none();
  ir_val_t destination = ir_val_vreg(temporary, IR_TY_PTR);
  ir_val_t condition_value = hir_ir_build_condition_value(
      context, condition);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_block_t *true_block = hir_ir_cfg_new_block(context);
  ir_block_t *false_block = hir_ir_cfg_new_block(context);
  ir_block_t *merge_block = hir_ir_cfg_new_block(context);
  if (!true_block || !false_block || !merge_block ||
      !hir_ir_emit_conditional_branch(
          context, condition_value, true_block, false_block) ||
      !hir_ir_cfg_switch_to_block(context, true_block) ||
      !copy_aggregate_value_to(
          context, if_true, destination, result_type) ||
      !hir_ir_cfg_emit_branch(context, merge_block) ||
      !hir_ir_cfg_switch_to_block(context, false_block) ||
      !copy_aggregate_value_to(
          context, if_false, destination, result_type) ||
      !hir_ir_cfg_emit_branch(context, merge_block) ||
      !hir_ir_cfg_switch_to_block(context, merge_block))
    return ir_val_none();
  return destination;
}

ir_val_t hir_ir_aggregate_value_address(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  if (!node) return hir_ir_unsupported_expr(context);
  psx_hir_node_kind_t kind = psx_hir_node_kind(node);
  ir_mir_type_info_t type = hir_ir_classify_node_type(context, node);
  if (hir_ir_node_is_lvalue(node)) {
    ir_val_t pointer = hir_ir_lvalue_address(context, node);
    if (context->status != IR_HIR_BUILD_OK)
      return ir_val_none();
    int atomic_width =
        hir_ir_atomic_object_storage_width(
            context, node, type);
    if (atomic_width != 0)
      return hir_ir_load_atomic_object_value(
          context, pointer, atomic_width);
    if ((psx_hir_node_qual_type(node).qualifiers &
         PSX_TYPE_QUALIFIER_VOLATILE) != 0)
      return materialize_volatile_aggregate_lvalue(
          context, node, pointer, type);
    return pointer;
  }
  if (kind == PSX_HIR_STMT_EXPR) {
    const psx_hir_node_t *prefix = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    const psx_hir_node_t *value = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_RHS, 0);
    if (!prefix || psx_hir_node_kind(prefix) != PSX_HIR_BLOCK ||
        !value || !hir_ir_build_statement(context, prefix))
      return hir_ir_unsupported_expr(context);
    return hir_ir_aggregate_value_address(context, value);
  }
  if (kind == PSX_HIR_CALL)
    return hir_ir_build_call(context, node, type);
  if (kind == PSX_HIR_CAST) {
    const psx_hir_node_t *operand = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    if (!operand) return hir_ir_unsupported_expr(context);
    ir_mir_type_info_t operand_type = hir_ir_classify_node_type(
        context, operand);
    if (operand_type.type_class != IR_MIR_TYPE_AGGREGATE ||
        operand_type.source_size != type.source_size)
      return hir_ir_unsupported_expr(context);
    return hir_ir_aggregate_value_address(context, operand);
  }
  if (kind == PSX_HIR_ASSIGN)
    return hir_ir_build_aggregate_assignment(
        context, node, hir_ir_classify_node_type(context, node));
  if (kind == PSX_HIR_TERNARY)
    return build_aggregate_ternary_address(
        context, node, hir_ir_classify_node_type(context, node));
  if (kind == PSX_HIR_COMMA) {
    const psx_hir_node_t *left = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    const psx_hir_node_t *right = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_RHS, 0);
    if (!left || !right) return hir_ir_unsupported_expr(context);
    (void)hir_ir_build_expr(context, left);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    return hir_ir_aggregate_value_address(context, right);
  }
  return hir_ir_unsupported_expr(context);
}
