#include "hir_ir_builder_internal.h"
#include "../type_layout.h"

#include <stdlib.h>

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
  int copy_size = target
      ? ps_type_sizeof_id(
            context->options->semantic_types,
            context->options->record_layouts,
            psx_hir_node_qual_type(target).type_id,
            context->options->target)
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
  ir_val_t condition_value = hir_ir_build_expr(context, condition);
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
  if (hir_ir_node_is_lvalue(node))
    return hir_ir_lvalue_address(context, node);
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
  ir_mir_type_info_t type = hir_ir_classify_node_type(context, node);
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
