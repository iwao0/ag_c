#include "hir_ir_builder_internal.h"

#include <string.h>

static int collect_switch_targets(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    hir_switch_target_t *target) {
  if (!node) return 1;
  if (psx_hir_node_kind(node) == PSX_HIR_SWITCH) return 1;
  if (psx_hir_node_kind(node) == PSX_HIR_CASE) {
    if (target->case_count >=
        sizeof(target->cases) / sizeof(target->cases[0])) {
      context->status = IR_HIR_BUILD_UNSUPPORTED;
      return 0;
    }
    ir_block_t *block = hir_ir_cfg_new_block(context);
    if (!block) return 0;
    target->cases[target->case_count++] =
        (hir_case_target_t){node, block};
  } else if (psx_hir_node_kind(node) == PSX_HIR_DEFAULT) {
    if (target->default_node) {
      context->status = IR_HIR_BUILD_INVALID;
      return 0;
    }
    target->default_node = node;
    target->default_block = hir_ir_cfg_new_block(context);
    if (!target->default_block) return 0;
  }
  for (size_t i = 0; i < psx_hir_node_child_count(node); i++) {
    const psx_hir_node_t *child = psx_hir_module_lookup(
        context->hir, psx_hir_node_child_at(node, i));
    if (!collect_switch_targets(context, child, target)) return 0;
  }
  return 1;
}

static ir_block_t *current_case_block(
    const hir_ir_context_t *context, const psx_hir_node_t *node) {
  if (context->switch_depth == 0) return NULL;
  const hir_switch_target_t *target =
      &context->switch_targets[context->switch_depth - 1];
  if (node == target->default_node) return target->default_block;
  for (size_t i = 0; i < target->case_count; i++) {
    if (target->cases[i].node == node) return target->cases[i].block;
  }
  return NULL;
}

static int build_switch_statement(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  const psx_hir_node_t *control = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *body = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  if (!control || !body || context->switch_depth >=
      sizeof(context->switch_targets) /
          sizeof(context->switch_targets[0])) {
    context->status = IR_HIR_BUILD_UNSUPPORTED;
    return 0;
  }
  ir_val_t control_value = hir_ir_build_expr(context, control);
  if (context->status != IR_HIR_BUILD_OK ||
      !hir_ir_is_integer_type(control_value.type))
    return 0;

  hir_switch_target_t *target =
      &context->switch_targets[context->switch_depth++];
  memset(target, 0, sizeof(*target));
  target->end_block = hir_ir_cfg_new_block(context);
  if (!target->end_block ||
      !collect_switch_targets(context, body, target)) {
    context->switch_depth--;
    return 0;
  }

  for (size_t i = 0; i < target->case_count; i++) {
    int compare_vreg = hir_ir_new_vreg(context);
    ir_block_t *next_block = hir_ir_cfg_new_block(context);
    if (compare_vreg < 0 || !next_block) {
      context->switch_depth--;
      return 0;
    }
    ir_inst_t *compare = ir_inst_new(IR_EQ);
    if (!compare) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      context->switch_depth--;
      return 0;
    }
    compare->dst = ir_val_vreg(compare_vreg, IR_TY_I32);
    compare->src1 = control_value;
    compare->src2 = ir_val_imm(
        control_value.type,
        psx_hir_node_integer_value(target->cases[i].node));
    if (!hir_ir_append_instruction(context, compare) ||
        !hir_ir_emit_conditional_branch(
            context, compare->dst, target->cases[i].block, next_block) ||
        !hir_ir_cfg_switch_to_block(context, next_block)) {
      context->switch_depth--;
      return 0;
    }
  }
  ir_block_t *fallback = target->default_block
                             ? target->default_block : target->end_block;
  ir_block_t *body_entry = hir_ir_cfg_new_block(context);
  ir_block_t *outer_continue = context->loop_depth
                                   ? context->loop_targets[
                                         context->loop_depth - 1]
                                         .continue_block
                                   : NULL;
  if (!body_entry || !hir_ir_cfg_emit_branch(context, fallback) ||
      !hir_ir_cfg_switch_to_block(context, body_entry) ||
      !hir_ir_cfg_push_loop(context, outer_continue, target->end_block)) {
    context->switch_depth--;
    return 0;
  }
  int built = hir_ir_build_statement(context, body);
  hir_ir_cfg_pop_loop(context);
  if (!built || !hir_ir_cfg_emit_branch(context, target->end_block) ||
      !hir_ir_cfg_switch_to_block(context, target->end_block)) {
    context->switch_depth--;
    return 0;
  }
  context->switch_depth--;
  return 1;
}

int hir_ir_build_statement(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  if (!node || context->status != IR_HIR_BUILD_OK) return 0;
  if (psx_hir_node_role(node) == PSX_HIR_ROLE_EXPRESSION) {
    (void)hir_ir_build_expr(context, node);
    return context->status == IR_HIR_BUILD_OK;
  }
  switch (psx_hir_node_kind(node)) {
    case PSX_HIR_BLOCK:
      for (size_t i = 0; i < psx_hir_node_child_count(node); i++) {
        if (psx_hir_node_child_edge_at(node, i) !=
            PSX_HIR_EDGE_BLOCK_ITEM) {
          context->status = IR_HIR_BUILD_INVALID;
          return 0;
        }
        const psx_hir_node_t *child = psx_hir_module_lookup(
            context->hir, psx_hir_node_child_at(node, i));
        if (hir_ir_cfg_current_block_terminated(context)) {
          psx_hir_node_kind_t child_kind = psx_hir_node_kind(child);
          if (child_kind != PSX_HIR_CASE &&
              child_kind != PSX_HIR_DEFAULT &&
              child_kind != PSX_HIR_LABEL) {
            ir_block_t *dead_block = hir_ir_cfg_new_block(context);
            if (!dead_block || !hir_ir_cfg_switch_to_block(context, dead_block))
              return 0;
          }
        }
        if (!hir_ir_build_statement(context, child)) return 0;
      }
      return 1;
    case PSX_HIR_RETURN: {
      const psx_hir_node_t *value = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_LHS, 0);
      if (!value) {
        if (!context->returns_void) {
          context->status = IR_HIR_BUILD_INVALID;
          return 0;
        }
        ir_inst_t *ret = ir_inst_new(IR_RET);
        if (!ret) {
          context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
          return 0;
        }
        ret->src1 = ir_val_none();
        return hir_ir_append_instruction(context, ret);
      }
      if (hir_ir_is_complex_type(context->return_info)) {
        ir_mir_type_info_t value_type = hir_ir_classify_node_type(
            context, value);
        ir_val_t result = hir_ir_materialize_complex_operand(
            context, value, context->return_info);
        if (context->status != IR_HIR_BUILD_OK ||
            !hir_ir_is_complex_type(value_type) ||
            result.type != IR_TY_PTR)
          return 0;
        ir_inst_t *ret = ir_inst_new(IR_RET);
        if (!ret) {
          context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
          return 0;
        }
        ret->src1 = result;
        return hir_ir_append_instruction(context, ret);
      }
      if (context->return_info.type_class == IR_MIR_TYPE_AGGREGATE) {
        ir_mir_type_info_t value_type = hir_ir_classify_node_type(
            context, value);
        ir_val_t source = hir_ir_aggregate_value_address(context, value);
        if (context->status != IR_HIR_BUILD_OK ||
            value_type.type_class != IR_MIR_TYPE_AGGREGATE ||
            value_type.source_size != context->return_info.source_size ||
            source.type != IR_TY_PTR)
          return 0;
        ir_inst_t *ret = ir_inst_new(IR_RET);
        if (!ret) {
          context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
          return 0;
        }
        ret->src1 = source;
        return hir_ir_append_instruction(context, ret);
      }
      if (context->returns_void) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      ir_val_t result = hir_ir_build_expr(context, value);
      if (context->status != IR_HIR_BUILD_OK) return 0;
      ir_mir_type_info_t value_type = hir_ir_classify_node_type(context, value);
      result = hir_ir_coerce_direct_value_to_qual_type(
          context, result, value_type, context->return_info,
          context->return_qual_type);
      if (context->status != IR_HIR_BUILD_OK) return 0;
      ir_inst_t *ret = ir_inst_new(IR_RET);
      if (!ret) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return 0;
      }
      ret->src1 = result;
      if (!hir_ir_append_instruction(context, ret)) return 0;
      return 1;
    }
    case PSX_HIR_VLA_ALLOC:
      (void)hir_ir_build_vla_allocation(context, node);
      return context->status == IR_HIR_BUILD_OK;
    case PSX_HIR_NOP:
      return 1;
    case PSX_HIR_IF: {
      const psx_hir_node_t *condition = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_LHS, 0);
      const psx_hir_node_t *then_statement = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_RHS, 0);
      const psx_hir_node_t *else_statement = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_ELSE, 0);
      if (!condition || !then_statement) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      ir_val_t value = hir_ir_build_expr(context, condition);
      if (context->status != IR_HIR_BUILD_OK) return 0;
      ir_block_t *then_block = hir_ir_cfg_new_block(context);
      ir_block_t *merge_block = hir_ir_cfg_new_block(context);
      ir_block_t *else_block = else_statement
                                   ? hir_ir_cfg_new_block(context) : merge_block;
      if (!then_block || !merge_block || !else_block ||
          !hir_ir_emit_conditional_branch(
              context, value, then_block, else_block) ||
          !hir_ir_cfg_switch_to_block(context, then_block) ||
          !hir_ir_build_statement(context, then_statement) ||
          !hir_ir_cfg_emit_branch(context, merge_block)) return 0;
      if (else_statement &&
          (!hir_ir_cfg_switch_to_block(context, else_block) ||
           !hir_ir_build_statement(context, else_statement) ||
           !hir_ir_cfg_emit_branch(context, merge_block))) return 0;
      return hir_ir_cfg_switch_to_block(context, merge_block);
    }
    case PSX_HIR_WHILE:
    case PSX_HIR_DO_WHILE: {
      const psx_hir_node_t *condition = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_LHS, 0);
      const psx_hir_node_t *body = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_RHS, 0);
      if (!condition || !body) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      ir_block_t *condition_block = hir_ir_cfg_new_block(context);
      ir_block_t *body_block = hir_ir_cfg_new_block(context);
      ir_block_t *exit_block = hir_ir_cfg_new_block(context);
      if (!condition_block || !body_block || !exit_block) return 0;
      if (psx_hir_node_kind(node) == PSX_HIR_WHILE) {
        if (!hir_ir_cfg_emit_branch(context, condition_block) ||
            !hir_ir_cfg_switch_to_block(context, condition_block)) return 0;
        ir_val_t value = hir_ir_build_expr(context, condition);
        if (context->status != IR_HIR_BUILD_OK ||
            !hir_ir_emit_conditional_branch(
                context, value, body_block, exit_block)) return 0;
      } else if (!hir_ir_cfg_emit_branch(context, body_block)) {
        return 0;
      }
      if (!hir_ir_cfg_push_loop(context, condition_block, exit_block) ||
          !hir_ir_cfg_switch_to_block(context, body_block) ||
          !hir_ir_build_statement(context, body)) return 0;
      hir_ir_cfg_pop_loop(context);
      if (!hir_ir_cfg_emit_branch(context, condition_block)) return 0;
      if (psx_hir_node_kind(node) == PSX_HIR_DO_WHILE) {
        if (!hir_ir_cfg_switch_to_block(context, condition_block)) return 0;
        ir_val_t value = hir_ir_build_expr(context, condition);
        if (context->status != IR_HIR_BUILD_OK ||
            !hir_ir_emit_conditional_branch(
                context, value, body_block, exit_block)) return 0;
      }
      return hir_ir_cfg_switch_to_block(context, exit_block);
    }
    case PSX_HIR_FOR: {
      const psx_hir_node_t *initial = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_INIT, 0);
      const psx_hir_node_t *condition = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_LHS, 0);
      const psx_hir_node_t *increment = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_INCREMENT, 0);
      const psx_hir_node_t *body = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_RHS, 0);
      if (!body) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      if (initial && !hir_ir_build_statement(context, initial)) return 0;
      ir_block_t *condition_block = hir_ir_cfg_new_block(context);
      ir_block_t *body_block = hir_ir_cfg_new_block(context);
      ir_block_t *increment_block = hir_ir_cfg_new_block(context);
      ir_block_t *exit_block = hir_ir_cfg_new_block(context);
      if (!condition_block || !body_block || !increment_block ||
          !exit_block || !hir_ir_cfg_emit_branch(context, condition_block) ||
          !hir_ir_cfg_switch_to_block(context, condition_block)) return 0;
      if (condition) {
        ir_val_t value = hir_ir_build_expr(context, condition);
        if (context->status != IR_HIR_BUILD_OK ||
            !hir_ir_emit_conditional_branch(
                context, value, body_block, exit_block)) return 0;
      } else if (!hir_ir_cfg_emit_branch(context, body_block)) {
        return 0;
      }
      if (!hir_ir_cfg_push_loop(context, increment_block, exit_block) ||
          !hir_ir_cfg_switch_to_block(context, body_block) ||
          !hir_ir_build_statement(context, body)) return 0;
      hir_ir_cfg_pop_loop(context);
      if (!hir_ir_cfg_emit_branch(context, increment_block) ||
          !hir_ir_cfg_switch_to_block(context, increment_block)) return 0;
      if (increment && !hir_ir_build_statement(context, increment)) return 0;
      if (!hir_ir_cfg_emit_branch(context, condition_block)) return 0;
      return hir_ir_cfg_switch_to_block(context, exit_block);
    }
    case PSX_HIR_SWITCH:
      return build_switch_statement(context, node);
    case PSX_HIR_CASE:
    case PSX_HIR_DEFAULT: {
      ir_block_t *target = current_case_block(context, node);
      const psx_hir_node_t *statement = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_RHS, 0);
      if (!target) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      if (!hir_ir_cfg_emit_branch(context, target) ||
          !hir_ir_cfg_switch_to_block(context, target)) return 0;
      return !statement || hir_ir_build_statement(context, statement);
    }
    case PSX_HIR_BREAK:
    case PSX_HIR_CONTINUE: {
      if (context->loop_depth == 0) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      hir_loop_target_t target =
          context->loop_targets[context->loop_depth - 1];
      ir_block_t *destination =
          psx_hir_node_kind(node) == PSX_HIR_BREAK
              ? target.break_block : target.continue_block;
      ir_block_t *dead_block = hir_ir_cfg_new_block(context);
      return dead_block && hir_ir_cfg_emit_branch(context, destination) &&
             hir_ir_cfg_switch_to_block(context, dead_block);
    }
    case PSX_HIR_GOTO: {
      size_t name_length = 0;
      const char *name = psx_hir_node_name(node, &name_length);
      ir_block_t *target = hir_ir_cfg_lookup_label(
          context, name, name_length);
      ir_block_t *dead_block = hir_ir_cfg_new_block(context);
      if (!target || !dead_block) {
        if (context->status == IR_HIR_BUILD_OK)
          context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      return hir_ir_cfg_emit_branch(context, target) &&
             hir_ir_cfg_switch_to_block(context, dead_block);
    }
    case PSX_HIR_LABEL: {
      size_t name_length = 0;
      const char *name = psx_hir_node_name(node, &name_length);
      ir_block_t *target = hir_ir_cfg_lookup_label(
          context, name, name_length);
      const psx_hir_node_t *statement = hir_ir_child_for_edge(
          context, node, PSX_HIR_EDGE_RHS, 0);
      if (!target) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      if (!hir_ir_cfg_emit_branch(context, target) ||
          !hir_ir_cfg_switch_to_block(context, target)) return 0;
      return !statement || hir_ir_build_statement(context, statement);
    }
    default:
      context->status = IR_HIR_BUILD_UNSUPPORTED;
      return 0;
  }
}
