#include "hir_ir_builder_internal.h"

#include <string.h>

static int append_instruction(
    hir_ir_context_t *context, ir_inst_t *instruction) {
  if (!instruction) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  ir_func_append_inst(context->function, instruction);
  return 1;
}

int hir_ir_cfg_current_block_terminated(const hir_ir_context_t *context) {
  if (!context->function->cur_block ||
      !context->function->cur_block->tail)
    return 0;
  ir_op_t op = context->function->cur_block->tail->op;
  return op == IR_BR || op == IR_BR_COND || op == IR_RET;
}

int hir_ir_cfg_block_has_predecessor(
    const ir_func_t *function, const ir_block_t *target) {
  if (!function || !target) return 0;
  for (const ir_block_t *block = function->entry;
       block; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if ((instruction->op == IR_BR ||
           instruction->op == IR_BR_COND) &&
          instruction->label_id == target->id)
        return 1;
      if (instruction->op == IR_BR_COND &&
          instruction->else_label_id == target->id)
        return 1;
    }
  }
  return 0;
}

ir_block_t *hir_ir_cfg_new_block(hir_ir_context_t *context) {
  ir_block_t *block = ir_block_new(context->function);
  if (!block) context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
  return block;
}

int hir_ir_cfg_switch_to_block(
    hir_ir_context_t *context, ir_block_t *block) {
  if (!block) return 0;
  ir_func_switch_block(context->function, block);
  ir_inst_t *label = ir_inst_new(IR_LABEL);
  if (!label) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  label->label_id = block->id;
  return append_instruction(context, label);
}

int hir_ir_cfg_emit_branch(
    hir_ir_context_t *context, ir_block_t *target) {
  if (hir_ir_cfg_current_block_terminated(context)) return 1;
  ir_inst_t *branch = ir_inst_new(IR_BR);
  if (!branch) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  branch->label_id = target->id;
  return append_instruction(context, branch);
}

int hir_ir_cfg_push_loop(
    hir_ir_context_t *context, ir_block_t *continue_block,
    ir_block_t *break_block) {
  if (context->loop_depth >=
      sizeof(context->loop_targets) / sizeof(context->loop_targets[0])) {
    context->status = IR_HIR_BUILD_UNSUPPORTED;
    return 0;
  }
  context->loop_targets[context->loop_depth++] =
      (hir_loop_target_t){continue_block, break_block};
  return 1;
}

void hir_ir_cfg_pop_loop(hir_ir_context_t *context) {
  if (context->loop_depth) context->loop_depth--;
}

ir_block_t *hir_ir_cfg_lookup_label(
    const hir_ir_context_t *context, const char *name,
    size_t name_length) {
  if (!name || name_length == 0) return NULL;
  for (size_t i = 0; i < context->label_count; i++) {
    const hir_label_target_t *target = &context->label_targets[i];
    if (target->name_length == name_length &&
        memcmp(target->name, name, name_length) == 0)
      return target->block;
  }
  return NULL;
}

int hir_ir_cfg_collect_labels(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  if (!node) return 1;
  if (psx_hir_node_kind(node) == PSX_HIR_LABEL) {
    size_t name_length = 0;
    const char *name = psx_hir_node_name(node, &name_length);
    if (!name || name_length == 0 ||
        context->label_count >=
            sizeof(context->label_targets) /
                sizeof(context->label_targets[0])) {
      context->status = IR_HIR_BUILD_INVALID;
      return 0;
    }
    if (hir_ir_cfg_lookup_label(context, name, name_length)) {
      context->status = IR_HIR_BUILD_INVALID;
      return 0;
    }
    ir_block_t *block = hir_ir_cfg_new_block(context);
    if (!block) return 0;
    context->label_targets[context->label_count++] =
        (hir_label_target_t){name, name_length, block};
  }
  for (size_t i = 0; i < psx_hir_node_child_count(node); i++) {
    const psx_hir_node_t *child = psx_hir_module_lookup(
        context->hir, psx_hir_node_child_at(node, i));
    if (!hir_ir_cfg_collect_labels(context, child)) return 0;
  }
  return 1;
}
