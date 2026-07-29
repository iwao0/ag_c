#include "hir_ir_builder_internal.h"

#include <stdint.h>
#include <stdlib.h>

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

static void enqueue_successor(
    ir_block_t **blocks_by_id, size_t block_capacity,
    unsigned char *visited, ir_block_t **queue, size_t *queue_count,
    int block_id) {
  if (block_id < 0 || (size_t)block_id >= block_capacity ||
      !blocks_by_id[block_id] || visited[block_id])
    return;
  visited[block_id] = 1;
  queue[(*queue_count)++] = blocks_by_id[block_id];
}

int hir_ir_cfg_block_end_reachable_without_noreturn(
    const ir_func_t *function, const ir_block_t *target) {
  if (!function || !function->entry || !target ||
      function->next_block_id <= 0)
    return 0;
  size_t block_capacity = (size_t)function->next_block_id;
  ir_block_t **blocks_by_id =
      calloc(block_capacity, sizeof(*blocks_by_id));
  unsigned char *visited = calloc(block_capacity, sizeof(*visited));
  ir_block_t **queue = calloc(block_capacity, sizeof(*queue));
  if (!blocks_by_id || !visited || !queue) {
    free(blocks_by_id);
    free(visited);
    free(queue);
    return 1;
  }
  for (ir_block_t *block = function->entry; block; block = block->next) {
    if (block->id >= 0 && (size_t)block->id < block_capacity)
      blocks_by_id[block->id] = block;
  }
  size_t queue_index = 0;
  size_t queue_count = 0;
  enqueue_successor(
      blocks_by_id, block_capacity, visited, queue, &queue_count,
      function->entry->id);
  int reachable = 0;
  while (queue_index < queue_count) {
    ir_block_t *block = queue[queue_index++];
    int terminated_by_noreturn = 0;
    for (ir_inst_t *instruction = block->head; instruction;
         instruction = instruction->next) {
      if (instruction->op == IR_CALL &&
          instruction->is_noreturn_call) {
        terminated_by_noreturn = 1;
        break;
      }
    }
    if (terminated_by_noreturn) continue;
    if (block == target) {
      reachable = 1;
      break;
    }
    for (ir_inst_t *instruction = block->head; instruction;
         instruction = instruction->next) {
      if (instruction->op != IR_BR &&
          instruction->op != IR_BR_COND &&
          instruction->op != IR_CONTINUATION_SUSPEND)
        continue;
      if (instruction->op == IR_BR_COND &&
          instruction->src1.id == IR_VAL_IMM) {
        enqueue_successor(
            blocks_by_id, block_capacity, visited, queue, &queue_count,
            instruction->src1.imm
                ? instruction->label_id
                : instruction->else_label_id);
        continue;
      }
      enqueue_successor(
          blocks_by_id, block_capacity, visited, queue, &queue_count,
          instruction->label_id);
      if (instruction->op == IR_BR_COND ||
          instruction->op == IR_CONTINUATION_SUSPEND)
        enqueue_successor(
            blocks_by_id, block_capacity, visited, queue, &queue_count,
            instruction->else_label_id);
    }
  }
  free(blocks_by_id);
  free(visited);
  free(queue);
  return reachable;
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
    ir_block_t *break_block, int continue_stack_checkpoint,
    int break_stack_checkpoint) {
  if (context->loop_depth == context->loop_capacity) {
    size_t capacity =
        context->loop_capacity ? context->loop_capacity * 2 : 16;
    if (capacity < context->loop_capacity ||
        capacity > SIZE_MAX / sizeof(*context->loop_targets)) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return 0;
    }
    hir_loop_target_t *targets = realloc(
        context->loop_targets, capacity * sizeof(*targets));
    if (!targets) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return 0;
    }
    context->loop_targets = targets;
    context->loop_capacity = capacity;
  }
  context->loop_targets[context->loop_depth++] =
      (hir_loop_target_t){
          continue_block, break_block,
          continue_stack_checkpoint, break_stack_checkpoint};
  return 1;
}

void hir_ir_cfg_pop_loop(hir_ir_context_t *context) {
  if (context->loop_depth) context->loop_depth--;
}

int hir_ir_emit_stack_save(
    hir_ir_context_t *context, int *checkpoint) {
  int vreg = hir_ir_new_vreg(context);
  ir_inst_t *save = vreg >= 0 ? ir_inst_new(IR_STACK_SAVE) : NULL;
  if (!save) {
    if (context->status == IR_HIR_BUILD_OK)
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  save->dst = ir_val_vreg(vreg, IR_TY_PTR);
  if (!hir_ir_append_instruction(context, save)) return 0;
  *checkpoint = vreg;
  return 1;
}

int hir_ir_emit_stack_restore(
    hir_ir_context_t *context, int checkpoint) {
  if (checkpoint < 0) return 1;
  ir_inst_t *restore = ir_inst_new(IR_STACK_RESTORE);
  if (!restore) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  restore->src1 = ir_val_vreg(checkpoint, IR_TY_PTR);
  return hir_ir_append_instruction(context, restore);
}

static int reserve_vla_lifetimes(
    hir_ir_context_t *context, unsigned lifetime_id) {
  size_t required = (size_t)lifetime_id + 1;
  if (required <= context->vla_lifetime_capacity) return 1;
  size_t capacity =
      context->vla_lifetime_capacity
          ? context->vla_lifetime_capacity : 16;
  while (capacity < required) {
    if (capacity > SIZE_MAX / 2) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return 0;
    }
    capacity *= 2;
  }
  if (capacity > SIZE_MAX / sizeof(*context->vla_lifetimes)) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  size_t prior_capacity = context->vla_lifetime_capacity;
  hir_vla_lifetime_state_t *states = realloc(
      context->vla_lifetimes, capacity * sizeof(*states));
  if (!states) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  context->vla_lifetimes = states;
  context->vla_lifetime_capacity = capacity;
  for (size_t i = prior_capacity; i < capacity; i++) {
    states[i] = (hir_vla_lifetime_state_t){
        .stack_checkpoint = -1,
    };
  }
  if (!states[0].registered) {
    states[0] = (hir_vla_lifetime_state_t){
        .parent_id = 0,
        .canonical_id = 0,
        .stack_checkpoint = -1,
        .registered = 1,
    };
  }
  return 1;
}

static hir_vla_lifetime_state_t *vla_lifetime_state(
    hir_ir_context_t *context, unsigned lifetime_id) {
  if (!reserve_vla_lifetimes(context, lifetime_id)) return NULL;
  hir_vla_lifetime_state_t *state =
      &context->vla_lifetimes[lifetime_id];
  if (!state->registered) {
    context->status = IR_HIR_BUILD_INVALID;
    return NULL;
  }
  return state;
}

int hir_ir_vla_lifetime_register(
    hir_ir_context_t *context, unsigned lifetime_id,
    unsigned parent_id) {
  if (!context || lifetime_id == 0 ||
      !reserve_vla_lifetimes(context, lifetime_id) ||
      !reserve_vla_lifetimes(context, parent_id))
    return 0;
  hir_vla_lifetime_state_t *parent =
      &context->vla_lifetimes[parent_id];
  hir_vla_lifetime_state_t *state =
      &context->vla_lifetimes[lifetime_id];
  if (!parent->registered || state->registered) {
    context->status = IR_HIR_BUILD_INVALID;
    return 0;
  }
  *state = (hir_vla_lifetime_state_t){
      .parent_id = parent_id,
      .canonical_id = parent->canonical_id,
      .stack_checkpoint = -1,
      .registered = 1,
  };
  return 1;
}

int hir_ir_vla_lifetime_prepare_allocation(
    hir_ir_context_t *context, unsigned lifetime_id) {
  hir_vla_lifetime_state_t *state =
      vla_lifetime_state(context, lifetime_id);
  if (!state) return 0;
  hir_vla_lifetime_state_t *canonical =
      vla_lifetime_state(context, state->canonical_id);
  if (!canonical) return 0;
  return canonical->stack_checkpoint >= 0 ||
         hir_ir_emit_stack_save(
             context, &canonical->stack_checkpoint);
}

int hir_ir_vla_lifetime_finish_allocation(
    hir_ir_context_t *context, unsigned lifetime_id) {
  hir_vla_lifetime_state_t *state =
      vla_lifetime_state(context, lifetime_id);
  if (!state || state->canonical_id == lifetime_id) {
    if (context && context->status == IR_HIR_BUILD_OK)
      context->status = IR_HIR_BUILD_INVALID;
    return 0;
  }
  if (!hir_ir_emit_stack_save(
          context, &state->stack_checkpoint))
    return 0;
  state->canonical_id = lifetime_id;
  return 1;
}

int hir_ir_vla_lifetime_restore(
    hir_ir_context_t *context, unsigned target_lifetime_id) {
  hir_vla_lifetime_state_t *current = vla_lifetime_state(
      context, context->current_vla_lifetime_id);
  hir_vla_lifetime_state_t *target = vla_lifetime_state(
      context, target_lifetime_id);
  if (!current || !target) return 0;
  if (current->canonical_id == target->canonical_id) return 1;
  hir_vla_lifetime_state_t *canonical =
      vla_lifetime_state(context, target->canonical_id);
  if (!canonical || canonical->stack_checkpoint < 0) {
    if (context->status == IR_HIR_BUILD_OK)
      context->status = IR_HIR_BUILD_INVALID;
    return 0;
  }
  return hir_ir_emit_stack_restore(
      context, canonical->stack_checkpoint);
}

ir_block_t *hir_ir_cfg_lookup_label(
    const hir_ir_context_t *context, int label_id) {
  if (!context || label_id <= 0) return NULL;
  for (size_t i = 0; i < context->label_count; i++) {
    const hir_label_target_t *target = &context->label_targets[i];
    if (target->label_id == label_id) return target->block;
  }
  return NULL;
}

int hir_ir_cfg_collect_labels(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  if (!node) return 1;
  if (psx_hir_node_kind(node) == PSX_HIR_LABEL) {
    int label_id = psx_hir_node_label_id(node);
    if (label_id <= 0) {
      context->status = IR_HIR_BUILD_INVALID;
      return 0;
    }
    if (hir_ir_cfg_lookup_label(context, label_id)) {
      context->status = IR_HIR_BUILD_INVALID;
      return 0;
    }
    ir_block_t *block = hir_ir_cfg_new_block(context);
    if (!block) return 0;
    if (context->label_count == context->label_capacity) {
      size_t capacity =
          context->label_capacity
              ? context->label_capacity * 2 : 32;
      if (capacity < context->label_capacity ||
          capacity > SIZE_MAX / sizeof(*context->label_targets)) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return 0;
      }
      hir_label_target_t *targets = realloc(
          context->label_targets, capacity * sizeof(*targets));
      if (!targets) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return 0;
      }
      context->label_targets = targets;
      context->label_capacity = capacity;
    }
    context->label_targets[context->label_count++] =
        (hir_label_target_t){label_id, block};
  }
  for (size_t i = 0; i < psx_hir_node_child_count(node); i++) {
    const psx_hir_node_t *child = psx_hir_module_lookup(
        context->hir, psx_hir_node_child_at(node, i));
    if (!hir_ir_cfg_collect_labels(context, child)) return 0;
  }
  return 1;
}
