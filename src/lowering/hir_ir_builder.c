#include "hir_ir_builder.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "abi_lowering.h"
#include "ir_builder.h"
#include "../diag/diag.h"
#include "../parser/type.h"
#include "../semantic/type_identity.h"
#include "../type_layout.h"

typedef struct {
  int object_offset;
  int pointer_vreg;
} hir_local_slot_t;

typedef struct {
  ir_block_t *continue_block;
  ir_block_t *break_block;
} hir_loop_target_t;

typedef struct {
  const psx_hir_node_t *node;
  ir_block_t *block;
} hir_case_target_t;

typedef struct {
  const char *name;
  size_t name_length;
  ir_block_t *block;
} hir_label_target_t;

typedef struct {
  hir_case_target_t cases[128];
  size_t case_count;
  const psx_hir_node_t *default_node;
  ir_block_t *default_block;
  ir_block_t *end_block;
} hir_switch_target_t;

typedef struct {
  const psx_hir_module_t *hir;
  const ir_build_options_t *options;
  ir_module_t *module;
  ir_func_t *function;
  ir_hir_build_status_t status;
  ir_abi_param_info_t return_info;
  int returns_void;
  hir_local_slot_t local_slots[512];
  size_t local_slot_count;
  hir_loop_target_t loop_targets[32];
  size_t loop_depth;
  hir_switch_target_t switch_targets[16];
  size_t switch_depth;
  hir_label_target_t label_targets[128];
  size_t label_count;
} hir_ir_context_t;

static const psx_hir_node_t *child_for_edge(
    const hir_ir_context_t *context, const psx_hir_node_t *node,
    psx_hir_edge_kind_t edge, size_t occurrence) {
  size_t seen = 0;
  for (size_t i = 0; i < psx_hir_node_child_count(node); i++) {
    if (psx_hir_node_child_edge_at(node, i) != edge) continue;
    if (seen++ != occurrence) continue;
    return psx_hir_module_lookup(
        context->hir, psx_hir_node_child_at(node, i));
  }
  return NULL;
}

static size_t child_count_for_edge(
    const psx_hir_node_t *node, psx_hir_edge_kind_t edge) {
  size_t count = 0;
  for (size_t i = 0; i < psx_hir_node_child_count(node); i++) {
    if (psx_hir_node_child_edge_at(node, i) == edge) count++;
  }
  return count;
}

static ir_abi_param_info_t classify_node_type(
    const hir_ir_context_t *context, const psx_hir_node_t *node) {
  ir_abi_type_context_t abi = {
      .semantic_types = context->options->semantic_types,
      .record_layouts = context->options->record_layouts,
      .target = context->options->target,
  };
  return ir_abi_classify_type_id(
      &abi, psx_hir_node_qual_type(node).type_id);
}

static psx_type_kind_t node_type_kind(
    const hir_ir_context_t *context, const psx_hir_node_t *node) {
  const psx_type_t *type = node
                               ? psx_semantic_type_table_lookup(
                                     context->options->semantic_types,
                                     psx_hir_node_qual_type(node).type_id)
                               : NULL;
  return type ? type->kind : PSX_TYPE_INVALID;
}

static ir_val_t unsupported_expr(hir_ir_context_t *context) {
  context->status = IR_HIR_BUILD_UNSUPPORTED;
  return ir_val_none();
}

static int new_vreg(hir_ir_context_t *context) {
  int vreg = ir_func_new_vreg(context->function);
  if (vreg < 0) context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
  return vreg;
}

static int append_instruction(
    hir_ir_context_t *context, ir_inst_t *instruction) {
  if (!instruction) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  ir_func_append_inst(context->function, instruction);
  return 1;
}

static void set_callable_signature(
    ir_inst_t *instruction, const ir_callable_sig_t *signature) {
  instruction->callable_sig.result = signature->result;
  instruction->callable_sig.param_count = signature->param_count;
  instruction->callable_sig.is_variadic = signature->is_variadic;
  for (int i = 0; i < signature->param_count; i++)
    instruction->callable_sig.params[i] = signature->params[i];
  instruction->has_callable_sig = 1;
}

static int is_integer_ir_type(ir_type_t type);
static int is_float_ir_type(ir_type_t type);
static int is_direct_value_abi_type(ir_abi_param_info_t type);
static int block_has_predecessor(
    const ir_func_t *function, const ir_block_t *target);
static ir_val_t scalar_truth_value(
    hir_ir_context_t *context, ir_val_t value);
static ir_val_t pointer_with_offset(
    hir_ir_context_t *context, ir_val_t base, int offset);
static int store_direct_value(
    hir_ir_context_t *context, ir_val_t pointer, ir_val_t value);
static ir_val_t materialize_complex_operand(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_abi_param_info_t target_type);

static int current_block_is_terminated(const hir_ir_context_t *context) {
  if (!context->function->cur_block ||
      !context->function->cur_block->tail) return 0;
  ir_op_t op = context->function->cur_block->tail->op;
  return op == IR_BR || op == IR_BR_COND || op == IR_RET;
}

static int append_implicit_return(
    hir_ir_context_t *context, const char *name, size_t name_length) {
  if (current_block_is_terminated(context)) return 1;
  if (context->function->cur_block != context->function->entry &&
      !block_has_predecessor(
          context->function, context->function->cur_block))
    return 1;
  int is_main = name_length == 4 && memcmp(name, "main", 4) == 0;
  ir_inst_t *ret = ir_inst_new(IR_RET);
  if (!ret) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  if (context->returns_void) {
    ret->src1 = ir_val_none();
  } else if (is_direct_value_abi_type(context->return_info)) {
    ret->src1 = is_float_ir_type(context->return_info.type)
                    ? ir_val_fp_imm(context->return_info.type, 0.0)
                    : ir_val_imm(context->return_info.type, 0);
  } else {
    free(ret);
    context->status = IR_HIR_BUILD_UNSUPPORTED;
    return 0;
  }
  if (!is_main && !context->returns_void &&
      context->options->diagnostic_context) {
    diag_warn_tokf_in(
        context->options->diagnostic_context,
        DIAG_WARN_PARSER_MISSING_RETURN, NULL,
        diag_warn_message_for_in(
            context->options->diagnostic_context,
            DIAG_WARN_PARSER_MISSING_RETURN),
        (int)name_length, name);
  }
  return append_instruction(context, ret);
}

static int block_has_predecessor(
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

static ir_block_t *new_block(hir_ir_context_t *context) {
  ir_block_t *block = ir_block_new(context->function);
  if (!block) context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
  return block;
}

static int switch_to_block(
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

static int emit_branch(
    hir_ir_context_t *context, ir_block_t *target) {
  if (current_block_is_terminated(context)) return 1;
  ir_inst_t *branch = ir_inst_new(IR_BR);
  if (!branch) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  branch->label_id = target->id;
  return append_instruction(context, branch);
}

static int emit_conditional_branch(
    hir_ir_context_t *context, ir_val_t condition,
    ir_block_t *if_true, ir_block_t *if_false) {
  if (current_block_is_terminated(context)) return 1;
  if (is_float_ir_type(condition.type)) {
    condition = scalar_truth_value(context, condition);
    if (context->status != IR_HIR_BUILD_OK) return 0;
  }
  if (!is_integer_ir_type(condition.type) && condition.type != IR_TY_PTR) {
    context->status = IR_HIR_BUILD_UNSUPPORTED;
    return 0;
  }
  ir_inst_t *branch = ir_inst_new(IR_BR_COND);
  if (!branch) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  branch->src1 = condition;
  branch->label_id = if_true->id;
  branch->else_label_id = if_false->id;
  return append_instruction(context, branch);
}

static int push_loop(
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

static void pop_loop(hir_ir_context_t *context) {
  if (context->loop_depth) context->loop_depth--;
}

static ir_block_t *lookup_label_block(
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

static int collect_label_blocks(
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
    if (lookup_label_block(context, name, name_length)) {
      context->status = IR_HIR_BUILD_INVALID;
      return 0;
    }
    ir_block_t *block = new_block(context);
    if (!block) return 0;
    context->label_targets[context->label_count++] =
        (hir_label_target_t){name, name_length, block};
  }
  for (size_t i = 0; i < psx_hir_node_child_count(node); i++) {
    const psx_hir_node_t *child = psx_hir_module_lookup(
        context->hir, psx_hir_node_child_at(node, i));
    if (!collect_label_blocks(context, child)) return 0;
  }
  return 1;
}

static int is_integer_ir_type(ir_type_t type) {
  return type == IR_TY_I8 || type == IR_TY_I16 ||
         type == IR_TY_I32 || type == IR_TY_I64;
}

static int is_float_ir_type(ir_type_t type) {
  return type == IR_TY_F32 || type == IR_TY_F64;
}

static int is_scalar_abi_type(ir_abi_param_info_t type) {
  return type.param_class == IR_ABI_PARAM_INTEGER ||
         type.param_class == IR_ABI_PARAM_POINTER;
}

static int is_float_abi_type(ir_abi_param_info_t type) {
  return type.param_class == IR_ABI_PARAM_FLOAT &&
         is_float_ir_type(type.type) &&
         type.source_size == ir_type_size(type.type);
}

static int is_complex_abi_type(ir_abi_param_info_t type) {
  return type.param_class == IR_ABI_PARAM_FLOAT &&
         is_float_ir_type(type.type) &&
         type.source_size == 2 * ir_type_size(type.type);
}

static int is_scalar_value_abi_type(ir_abi_param_info_t type) {
  return is_scalar_abi_type(type) || is_float_abi_type(type);
}

static int is_register_aggregate_abi_type(ir_abi_param_info_t type) {
  return type.param_class == IR_ABI_PARAM_AGGREGATE &&
         (type.source_size == 4 || type.source_size == 8) &&
         (type.type == IR_TY_I32 || type.type == IR_TY_I64);
}

static int is_indirect_aggregate_abi_type(ir_abi_param_info_t type) {
  return type.param_class == IR_ABI_PARAM_AGGREGATE &&
         type.source_size > 0 && type.type == IR_TY_PTR;
}

static int is_direct_value_abi_type(ir_abi_param_info_t type) {
  return is_scalar_value_abi_type(type) ||
         is_register_aggregate_abi_type(type);
}

static ir_type_t integer_storage_type(ir_abi_param_info_t type) {
  if (type.source_size >= 8) return IR_TY_I64;
  if (type.source_size == 4) return IR_TY_I32;
  if (type.source_size == 2) return IR_TY_I16;
  return IR_TY_I8;
}

static ir_type_t scalar_storage_type(ir_abi_param_info_t type) {
  if (is_float_abi_type(type)) return type.type;
  return type.param_class == IR_ABI_PARAM_POINTER
             ? IR_TY_PTR : integer_storage_type(type);
}

static long long normalize_integer_immediate(
    long long value, int byte_size, int is_unsigned) {
  int bits = byte_size * 8;
  if (bits <= 0 || bits >= 64) return value;
  uint64_t mask = (UINT64_C(1) << bits) - 1;
  uint64_t normalized = (uint64_t)value & mask;
  if (!is_unsigned && (normalized & (UINT64_C(1) << (bits - 1))))
    normalized |= ~mask;
  return (long long)normalized;
}

static int find_local_address(
    const hir_ir_context_t *context, int object_offset) {
  for (size_t i = 0; i < context->local_slot_count; i++) {
    if (context->local_slots[i].object_offset == object_offset)
      return context->local_slots[i].pointer_vreg;
  }
  return -1;
}

static int local_storage_address(
    hir_ir_context_t *context, int object_offset, int size, int align) {
  int existing = find_local_address(context, object_offset);
  if (existing >= 0) return existing;
  if (context->local_slot_count >=
      sizeof(context->local_slots) / sizeof(context->local_slots[0])) {
    context->status = IR_HIR_BUILD_UNSUPPORTED;
    return -1;
  }
  if (size <= 0 || align <= 0 ||
      (align > 16 && size > INT_MAX - align)) {
    context->status = IR_HIR_BUILD_UNSUPPORTED;
    return -1;
  }
  int over_aligned = align > 16;
  int allocation_size = over_aligned ? size + align : size;
  int pointer = new_vreg(context);
  if (pointer < 0) return -1;
  ir_inst_t *alloca = ir_inst_new(IR_ALLOCA);
  if (!alloca) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return -1;
  }
  alloca->dst = ir_val_vreg(pointer, IR_TY_PTR);
  alloca->alloca_size = allocation_size;
  alloca->alloca_align = align;
  if (!append_instruction(context, alloca)) return -1;
  if (over_aligned) {
    int aligned_pointer = new_vreg(context);
    if (aligned_pointer < 0) return -1;
    ir_inst_t *align_pointer = ir_inst_new(IR_ALIGN_PTR);
    if (!align_pointer) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return -1;
    }
    align_pointer->dst = ir_val_vreg(aligned_pointer, IR_TY_PTR);
    align_pointer->src1 = ir_val_vreg(pointer, IR_TY_PTR);
    align_pointer->alloca_align = align;
    if (!append_instruction(context, align_pointer)) return -1;
    pointer = aligned_pointer;
  }
  context->local_slots[context->local_slot_count++] =
      (hir_local_slot_t){object_offset, pointer};
  return pointer;
}

static int local_owner_address_with_minimum(
    hir_ir_context_t *context, const psx_hir_node_t *local,
    int minimum_size, int minimum_align) {
  int object_offset = psx_hir_node_object_offset(local);
  ir_abi_param_info_t type = classify_node_type(context, local);
  int size = psx_hir_node_object_size(local);
  if (size <= 0) size = type.source_size;
  if (minimum_size > size) size = minimum_size;
  int align = psx_hir_node_object_align(local);
  if (align <= 0)
    align = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
  if (minimum_align > align) align = minimum_align;
  return local_storage_address(context, object_offset, size, align);
}

static int local_owner_address(
    hir_ir_context_t *context, const psx_hir_node_t *local) {
  return local_owner_address_with_minimum(context, local, 0, 0);
}

static int local_address_with_minimum(
    hir_ir_context_t *context, const psx_hir_node_t *local,
    int minimum_size, int minimum_align) {
  int base = local_owner_address_with_minimum(
      context, local, minimum_size, minimum_align);
  if (base < 0) return -1;
  int storage_offset = psx_hir_node_storage_offset(local);
  int object_offset = psx_hir_node_object_offset(local);
  int delta = storage_offset - object_offset;
  int object_size = psx_hir_node_object_size(local);
  if (delta == 0) return base;
  if (delta < 0 || object_size <= 0 || delta >= object_size) {
    context->status = IR_HIR_BUILD_INVALID;
    return -1;
  }
  int pointer = new_vreg(context);
  if (pointer < 0) return -1;
  ir_inst_t *lea = ir_inst_new(IR_LEA);
  if (!lea) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return -1;
  }
  lea->dst = ir_val_vreg(pointer, IR_TY_PTR);
  lea->src1 = ir_val_vreg(base, IR_TY_PTR);
  lea->src2 = ir_val_imm(IR_TY_I64, delta);
  if (!append_instruction(context, lea)) return -1;
  return pointer;
}

static int local_address(
    hir_ir_context_t *context, const psx_hir_node_t *local) {
  return local_address_with_minimum(context, local, 0, 0);
}

static int preallocate_local_storage(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  if (!node) return 1;
  if (psx_hir_node_kind(node) == PSX_HIR_LOCAL &&
      local_owner_address(context, node) < 0)
    return 0;
  for (size_t i = 0; i < psx_hir_node_child_count(node); i++) {
    const psx_hir_node_t *child = psx_hir_module_lookup(
        context->hir, psx_hir_node_child_at(node, i));
    if (!preallocate_local_storage(context, child)) return 0;
  }
  return 1;
}

static ir_val_t coerce_integer(
    hir_ir_context_t *context, ir_val_t value,
    ir_abi_param_info_t source, ir_abi_param_info_t target) {
  target.type = integer_storage_type(target);
  if (!is_integer_ir_type(value.type) ||
      !is_integer_ir_type(target.type))
    return unsupported_expr(context);
  if (value.type == target.type) return value;
  if (value.id == IR_VAL_IMM) {
    value.imm = normalize_integer_immediate(
        value.imm, target.source_size, target.is_unsigned);
    value.type = target.type;
    return value;
  }
  int result = new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_op_t op = ir_type_size(value.type) > ir_type_size(target.type)
                   ? IR_TRUNC
                   : source.is_unsigned ? IR_ZEXT : IR_SEXT;
  ir_inst_t *conversion = ir_inst_new(op);
  if (!conversion) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  conversion->dst = ir_val_vreg(result, target.type);
  conversion->src1 = value;
  if (!append_instruction(context, conversion)) return ir_val_none();
  return conversion->dst;
}

static ir_val_t coerce_scalar(
    hir_ir_context_t *context, ir_val_t value,
    ir_abi_param_info_t source, ir_abi_param_info_t target) {
  if (!is_scalar_abi_type(source) || !is_scalar_abi_type(target))
    return unsupported_expr(context);
  if (target.param_class == IR_ABI_PARAM_POINTER) {
    if (source.param_class == IR_ABI_PARAM_POINTER &&
        value.type == IR_TY_PTR)
      return value;
    if (source.param_class == IR_ABI_PARAM_INTEGER) {
      int source_size = source.source_size;
      int target_size = ag_target_info_pointer_size(
          context->options->target);
      if (source_size == target_size) {
        value.type = IR_TY_PTR;
        return value;
      }
      int result = new_vreg(context);
      if (result < 0) return ir_val_none();
      ir_inst_t *conversion = ir_inst_new(
          source_size > target_size ? IR_TRUNC : IR_ZEXT);
      if (!conversion) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return ir_val_none();
      }
      conversion->dst = ir_val_vreg(result, IR_TY_PTR);
      conversion->src1 = value;
      if (!append_instruction(context, conversion)) return ir_val_none();
      return conversion->dst;
    }
    return unsupported_expr(context);
  }
  if (source.param_class == IR_ABI_PARAM_POINTER) {
    int source_size = ag_target_info_pointer_size(
        context->options->target);
    int target_size = target.source_size;
    if (source_size == target_size) {
      value.type = target.type;
      return value;
    }
    int result = new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *conversion = ir_inst_new(
        source_size > target_size ? IR_TRUNC : IR_ZEXT);
    if (!conversion) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    conversion->dst = ir_val_vreg(result, target.type);
    conversion->src1 = value;
    if (!append_instruction(context, conversion)) return ir_val_none();
    return conversion->dst;
  }
  if (source.param_class != IR_ABI_PARAM_INTEGER)
    return unsupported_expr(context);
  return coerce_integer(context, value, source, target);
}

static ir_val_t coerce_direct_value(
    hir_ir_context_t *context, ir_val_t value,
    ir_abi_param_info_t source, ir_abi_param_info_t target) {
  if (is_register_aggregate_abi_type(source) &&
      is_register_aggregate_abi_type(target) &&
      source.source_size == target.source_size &&
      value.type == target.type)
    return value;
  if (!is_scalar_value_abi_type(source) ||
      !is_scalar_value_abi_type(target))
    return unsupported_expr(context);
  if (is_float_abi_type(source) || is_float_abi_type(target)) {
    if (value.type == target.type) return value;
    ir_op_t op;
    if (is_float_abi_type(source) && is_float_abi_type(target)) {
      op = IR_F2F;
    } else if (is_float_abi_type(source) &&
               target.param_class == IR_ABI_PARAM_INTEGER) {
      op = IR_F2I;
    } else if (source.param_class == IR_ABI_PARAM_INTEGER &&
               is_float_abi_type(target)) {
      op = IR_I2F;
    } else {
      return unsupported_expr(context);
    }
    int result = new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *conversion = ir_inst_new(op);
    if (!conversion) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    conversion->dst = ir_val_vreg(result, target.type);
    conversion->src1 = value;
    conversion->is_unsigned =
        (op == IR_I2F ? source.is_unsigned : target.is_unsigned) ? 1 : 0;
    if (!append_instruction(context, conversion)) return ir_val_none();
    return conversion->dst;
  }
  return coerce_scalar(context, value, source, target);
}

static int setup_scalar_parameters(
    hir_ir_context_t *context, const psx_hir_node_t *root,
    const ir_callable_sig_t *signature) {
  if (child_count_for_edge(root, PSX_HIR_EDGE_PARAMETER) !=
      signature->param_count) {
    context->status = IR_HIR_BUILD_INVALID;
    return 0;
  }
  int integer_index = 0;
  int float_index = 0;
  int abi_index = 0;
  for (size_t i = 0; i < signature->param_count; i++) {
    const psx_hir_node_t *parameter = child_for_edge(
        context, root, PSX_HIR_EDGE_PARAMETER, i);
    if (!parameter || psx_hir_node_kind(parameter) != PSX_HIR_LOCAL) {
      context->status = IR_HIR_BUILD_INVALID;
      return 0;
    }
    ir_abi_param_info_t type = classify_node_type(context, parameter);
    if ((!is_scalar_value_abi_type(type) &&
         !is_complex_abi_type(type) &&
         !is_register_aggregate_abi_type(type) &&
         !is_indirect_aggregate_abi_type(type)) ||
        signature->params[i] != type.type) {
      context->status = IR_HIR_BUILD_UNSUPPORTED;
      return 0;
    }
    int minimum_size =
        is_indirect_aggregate_abi_type(type) ? type.source_size : 0;
    int minimum_align =
        minimum_size >= 8 ? 8 :
        minimum_size >= 4 ? 4 :
        minimum_size >= 2 ? 2 :
        minimum_size > 0 ? 1 : 0;
    int pointer = local_address_with_minimum(
        context, parameter, minimum_size, minimum_align);
    if (pointer < 0) return 0;
    if (is_complex_abi_type(type)) {
      int half = ir_type_size(type.type);
      if (abi_index + 2 > 32) {
        context->status = IR_HIR_BUILD_UNSUPPORTED;
        return 0;
      }
      for (int part = 0; part < 2; part++) {
        int value_vreg = new_vreg(context);
        if (value_vreg < 0) return 0;
        ir_inst_t *input = ir_inst_new(IR_PARAM);
        if (!input) {
          context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
          return 0;
        }
        input->dst = ir_val_vreg(value_vreg, type.type);
        int parameter_index =
            ag_target_info_call_abi(context->options->target) ==
                    AG_TARGET_CALL_ABI_AAPCS64
                ? float_index++
                : abi_index;
        input->src1 = ir_val_imm(IR_TY_I32, parameter_index);
        if (!append_instruction(context, input)) return 0;
        ir_val_t destination = ir_val_vreg(pointer, IR_TY_PTR);
        if (part == 1)
          destination = pointer_with_offset(
              context, destination, half);
        if (context->status != IR_HIR_BUILD_OK ||
            !store_direct_value(context, destination, input->dst))
          return 0;
        context->function->param_abi_types[abi_index++] = type.type;
      }
      continue;
    }
    if (is_indirect_aggregate_abi_type(type)) {
      int source_vreg = new_vreg(context);
      if (source_vreg < 0) return 0;
      ir_inst_t *input = ir_inst_new(IR_PARAM);
      if (!input) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return 0;
      }
      input->dst = ir_val_vreg(source_vreg, IR_TY_PTR);
      int parameter_index =
          ag_target_info_call_abi(context->options->target) ==
                  AG_TARGET_CALL_ABI_AAPCS64
              ? integer_index++
              : abi_index;
      input->src1 = ir_val_imm(IR_TY_I32, parameter_index);
      if (!append_instruction(context, input)) return 0;
      ir_inst_t *copy = ir_inst_new(IR_MEMCPY);
      if (!copy) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return 0;
      }
      copy->src1 = ir_val_vreg(pointer, IR_TY_PTR);
      copy->src2 = input->dst;
      copy->alloca_size = type.source_size;
      if (!append_instruction(context, copy)) return 0;
      if (abi_index >= 32) {
        context->status = IR_HIR_BUILD_UNSUPPORTED;
        return 0;
      }
      context->function->param_abi_types[abi_index++] = IR_TY_PTR;
      continue;
    }
    int value_vreg = new_vreg(context);
    if (value_vreg < 0) return 0;
    ir_inst_t *input = ir_inst_new(IR_PARAM);
    if (!input) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return 0;
    }
    input->dst = ir_val_vreg(value_vreg, scalar_storage_type(type));
    int parameter_index;
    if (ag_target_info_call_abi(context->options->target) ==
        AG_TARGET_CALL_ABI_AAPCS64) {
      parameter_index = is_float_abi_type(type)
                            ? float_index++ : integer_index++;
    } else {
      parameter_index = abi_index;
    }
    input->src1 = ir_val_imm(IR_TY_I32, parameter_index);
    if (!append_instruction(context, input)) return 0;
    ir_inst_t *store = ir_inst_new(IR_STORE);
    if (!store) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return 0;
    }
    store->src1 = ir_val_vreg(pointer, IR_TY_PTR);
    store->src2 = input->dst;
    if (!append_instruction(context, store)) return 0;
    if (abi_index >= 32) {
      context->status = IR_HIR_BUILD_UNSUPPORTED;
      return 0;
    }
    context->function->param_abi_types[abi_index++] =
        signature->params[i];
  }
  context->function->param_abi_count = abi_index;
  context->function->nargs_fixed = abi_index;
  return 1;
}

static int emit_vla_stride_value(
    hir_ir_context_t *context, ir_val_t dimension,
    ir_val_t accumulated, int destination_offset, int slot_size) {
  int product_vreg = new_vreg(context);
  if (product_vreg < 0) return -1;
  ir_inst_t *multiply = ir_inst_new(IR_MUL);
  if (!multiply) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return -1;
  }
  multiply->dst = ir_val_vreg(product_vreg, IR_TY_I32);
  multiply->src1 = dimension;
  multiply->src2 = accumulated;
  if (!append_instruction(context, multiply)) return -1;

  int wide_vreg = new_vreg(context);
  if (wide_vreg < 0) return -1;
  ir_inst_t *extend = ir_inst_new(IR_ZEXT);
  if (!extend) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return -1;
  }
  extend->dst = ir_val_vreg(wide_vreg, IR_TY_I64);
  extend->src1 = multiply->dst;
  if (!append_instruction(context, extend)) return -1;

  int slot = local_storage_address(
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
  if (!append_instruction(context, store)) return -1;
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
  int source = find_local_address(context, source_offset);
  if (source < 0) {
    context->status = IR_HIR_BUILD_INVALID;
    return 0;
  }
  int value_vreg = new_vreg(context);
  if (value_vreg < 0) return 0;
  ir_inst_t *load = ir_inst_new(IR_LOAD);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  load->dst = ir_val_vreg(value_vreg, IR_TY_I32);
  load->src1 = ir_val_vreg(source, IR_TY_PTR);
  if (!append_instruction(context, load)) return 0;
  *result = load->dst;
  return 1;
}

static int emit_vla_parameter_strides(
    hir_ir_context_t *context, const psx_hir_node_t *root) {
  size_t parameter_count = child_count_for_edge(
      root, PSX_HIR_EDGE_PARAMETER);
  for (size_t parameter_index = 0;
       parameter_index < parameter_count; parameter_index++) {
    const psx_hir_node_t *parameter = child_for_edge(
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
      int source = find_local_address(
          context,
          psx_hir_node_vla_stride_source_offset(parameter));
      if (source < 0) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      int value_vreg = new_vreg(context);
      if (value_vreg < 0) return 0;
      ir_inst_t *load = ir_inst_new(IR_LOAD);
      if (!load) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return 0;
      }
      load->dst = ir_val_vreg(value_vreg, IR_TY_I32);
      load->src1 = ir_val_vreg(source, IR_TY_PTR);
      if (!append_instruction(context, load) ||
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

static ir_val_t build_expr(
    hir_ir_context_t *context, const psx_hir_node_t *node);
static int build_statement(
    hir_ir_context_t *context, const psx_hir_node_t *node);
static ir_val_t emit_integer_binary(
    hir_ir_context_t *context, ir_op_t op, ir_val_t left,
    ir_val_t right, ir_type_t type);
static ir_val_t emit_integer_width_conversion(
    hir_ir_context_t *context, ir_val_t value, ir_type_t target,
    int sign_extend);
static int allocate_scalar_temp(
    hir_ir_context_t *context, int size, int alignment);

static ir_val_t build_vla_allocation(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  const psx_hir_node_t *size_or_outer = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *row_stride_expr = child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  int descriptor_offset = psx_hir_node_storage_offset(node);
  int row_stride_offset =
      psx_hir_node_vla_stride_frame_offset(node);
  int slot_size = psx_hir_node_vla_stride_slot_size(node);
  if (!size_or_outer || descriptor_offset <= 0 || slot_size < 8 ||
      ((row_stride_offset != 0) != (row_stride_expr != NULL)))
    return unsupported_expr(context);

  ir_val_t total_size;
  if (row_stride_expr) {
    ir_val_t row_stride = build_expr(context, row_stride_expr);
    ir_val_t outer_count = build_expr(context, size_or_outer);
    if (context->status != IR_HIR_BUILD_OK ||
        !is_integer_ir_type(row_stride.type) ||
        !is_integer_ir_type(outer_count.type))
      return unsupported_expr(context);
    total_size = emit_integer_binary(
        context, IR_MUL, outer_count, row_stride, IR_TY_I32);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    ir_val_t wide_stride = emit_integer_width_conversion(
        context, row_stride, IR_TY_I64, 0);
    int row_stride_slot = local_storage_address(
        context, row_stride_offset, slot_size, 8);
    if (context->status != IR_HIR_BUILD_OK ||
        row_stride_slot < 0 ||
        !store_direct_value(
            context, ir_val_vreg(row_stride_slot, IR_TY_PTR),
            wide_stride))
      return ir_val_none();
  } else {
    total_size = build_expr(context, size_or_outer);
    if (context->status != IR_HIR_BUILD_OK ||
        !is_integer_ir_type(total_size.type))
      return unsupported_expr(context);
  }

  total_size = emit_integer_width_conversion(
      context, total_size, IR_TY_I32, 0);
  ir_val_t wide_size = emit_integer_width_conversion(
      context, total_size, IR_TY_I64, 0);
  int size_slot = local_storage_address(
      context, descriptor_offset + slot_size, slot_size, 8);
  if (context->status != IR_HIR_BUILD_OK || size_slot < 0 ||
      !store_direct_value(
          context, ir_val_vreg(size_slot, IR_TY_PTR), wide_size))
    return ir_val_none();

  int base_vreg = new_vreg(context);
  if (base_vreg < 0) return ir_val_none();
  ir_inst_t *allocation = ir_inst_new(IR_VLA_ALLOC);
  if (!allocation) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  allocation->dst = ir_val_vreg(base_vreg, IR_TY_PTR);
  allocation->src1 = total_size;
  if (!append_instruction(context, allocation))
    return ir_val_none();

  int base_slot = local_storage_address(
      context, descriptor_offset, slot_size, 8);
  if (base_slot < 0 ||
      !store_direct_value(
          context, ir_val_vreg(base_slot, IR_TY_PTR),
          allocation->dst))
    return ir_val_none();
  return ir_val_imm(IR_TY_I32, 0);
}

static const psx_hir_symbol_t *resolved_global_symbol(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  const psx_hir_symbol_t *symbol = psx_hir_module_symbol_lookup(
      context->hir, psx_hir_node_symbol_id(node));
  size_t node_name_length = 0;
  size_t symbol_name_length = 0;
  const char *node_name = psx_hir_node_name(node, &node_name_length);
  const char *symbol_name = psx_hir_symbol_name(
      symbol, &symbol_name_length);
  if (!symbol || !node_name || !symbol_name ||
      node_name_length == 0 || node_name_length > INT_MAX ||
      node_name_length != symbol_name_length ||
      memcmp(node_name, symbol_name, node_name_length) != 0 ||
      psx_hir_symbol_qual_type(symbol).type_id == PSX_TYPE_ID_INVALID ||
      psx_hir_symbol_byte_size(symbol) <= 0 ||
      psx_hir_symbol_alignment(symbol) <= 0) {
    context->status = IR_HIR_BUILD_INVALID;
    return NULL;
  }
  ir_symbol_t *ir_symbol = ir_module_find_symbol(
      context->module, symbol_name, (int)symbol_name_length);
  if (!ir_symbol) {
    ir_symbol = ir_module_add_symbol(
        context->module, symbol_name, (int)symbol_name_length);
    if (!ir_symbol) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return NULL;
    }
    ir_symbol->byte_size = psx_hir_symbol_byte_size(symbol);
    ir_symbol->alignment = psx_hir_symbol_alignment(symbol);
    ir_symbol->is_extern = psx_hir_symbol_is_extern(symbol) ? 1 : 0;
    ir_symbol->is_static = psx_hir_symbol_is_static(symbol) ? 1 : 0;
    ir_symbol->is_thread_local =
        psx_hir_symbol_is_thread_local(symbol) ? 1 : 0;
  }
  return symbol;
}

static ir_val_t global_address(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  const psx_hir_symbol_t *symbol = resolved_global_symbol(context, node);
  if (!symbol) return ir_val_none();
  size_t name_length = 0;
  const char *name = psx_hir_symbol_name(symbol, &name_length);
  int result = new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(
      psx_hir_symbol_is_thread_local(symbol)
          ? IR_LOAD_TLV_ADDR : IR_LOAD_SYM);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(result, IR_TY_PTR);
  load->sym = (char *)name;
  load->sym_len = (int)name_length;
  load->is_got_funcref = psx_hir_symbol_is_extern(symbol) ? 1 : 0;
  if (!append_instruction(context, load)) return ir_val_none();
  return load->dst;
}

static ir_val_t lvalue_address(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  if (!node) return unsupported_expr(context);
  if (psx_hir_node_kind(node) == PSX_HIR_LOCAL) {
    int pointer = local_address(context, node);
    return pointer < 0 ? ir_val_none()
                       : ir_val_vreg(pointer, IR_TY_PTR);
  }
  if (psx_hir_node_kind(node) == PSX_HIR_GLOBAL)
    return global_address(context, node);
  if (psx_hir_node_kind(node) == PSX_HIR_DEREF) {
    const psx_hir_node_t *pointer = child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    if (!pointer) return unsupported_expr(context);
    ir_val_t result = build_expr(context, pointer);
    if (context->status != IR_HIR_BUILD_OK || result.type != IR_TY_PTR)
      return unsupported_expr(context);
    return result;
  }
  return unsupported_expr(context);
}

static ir_val_t pointer_with_offset(
    hir_ir_context_t *context, ir_val_t base, int offset) {
  if (base.type != IR_TY_PTR || offset < 0)
    return unsupported_expr(context);
  if (offset == 0) return base;
  int pointer = new_vreg(context);
  if (pointer < 0) return ir_val_none();
  ir_inst_t *lea = ir_inst_new(IR_LEA);
  if (!lea) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  lea->dst = ir_val_vreg(pointer, IR_TY_PTR);
  lea->src1 = base;
  lea->src2 = ir_val_imm(IR_TY_I64, offset);
  if (!append_instruction(context, lea)) return ir_val_none();
  return lea->dst;
}

static ir_val_t build_complex_assignment(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_abi_param_info_t target_type) {
  const psx_hir_node_t *target = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *source_node = child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  if (!target || !source_node || !is_complex_abi_type(target_type) ||
      (psx_hir_node_kind(target) != PSX_HIR_LOCAL &&
       psx_hir_node_kind(target) != PSX_HIR_DEREF &&
       psx_hir_node_kind(target) != PSX_HIR_GLOBAL))
    return unsupported_expr(context);
  ir_val_t destination = lvalue_address(context, target);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_abi_param_info_t source_type = classify_node_type(
      context, source_node);
  if (is_complex_abi_type(source_type)) {
    ir_val_t source = materialize_complex_operand(
        context, source_node, target_type);
    if (context->status != IR_HIR_BUILD_OK || source.type != IR_TY_PTR)
      return unsupported_expr(context);
    ir_inst_t *copy = ir_inst_new(IR_MEMCPY);
    if (!copy) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    copy->src1 = destination;
    copy->src2 = source;
    copy->alloca_size = target_type.source_size;
    if (!append_instruction(context, copy)) return ir_val_none();
    return destination;
  }
  if (!is_scalar_value_abi_type(source_type))
    return unsupported_expr(context);
  ir_val_t real = build_expr(context, source_node);
  ir_abi_param_info_t component_type = {
      .type = target_type.type,
      .param_class = IR_ABI_PARAM_FLOAT,
      .source_size = ir_type_size(target_type.type),
  };
  if (context->status == IR_HIR_BUILD_OK)
    real = coerce_direct_value(
        context, real, source_type, component_type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_inst_t *store_real = ir_inst_new(IR_STORE);
  if (!store_real) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  store_real->src1 = destination;
  store_real->src2 = real;
  if (!append_instruction(context, store_real)) return ir_val_none();
  int zero_vreg = new_vreg(context);
  if (zero_vreg < 0) return ir_val_none();
  ir_inst_t *zero = ir_inst_new(IR_LOAD_FP_IMM);
  if (!zero) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  zero->dst = ir_val_vreg(zero_vreg, target_type.type);
  zero->src1 = ir_val_fp_imm(target_type.type, 0.0);
  if (!append_instruction(context, zero)) return ir_val_none();
  ir_val_t imaginary_pointer = pointer_with_offset(
      context, destination, component_type.source_size);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_inst_t *store_imaginary = ir_inst_new(IR_STORE);
  if (!store_imaginary) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  store_imaginary->src1 = imaginary_pointer;
  store_imaginary->src2 = zero->dst;
  if (!append_instruction(context, store_imaginary)) return ir_val_none();
  return destination;
}

static ir_val_t build_complex_comparison(
    hir_ir_context_t *context, psx_hir_node_kind_t kind,
    ir_val_t left, ir_val_t right, ir_abi_param_info_t type) {
  if ((kind != PSX_HIR_EQ && kind != PSX_HIR_NE) ||
      left.type != IR_TY_PTR || right.type != IR_TY_PTR ||
      !is_complex_abi_type(type))
    return unsupported_expr(context);
  int half = ir_type_size(type.type);
  ir_val_t left_imaginary = pointer_with_offset(context, left, half);
  ir_val_t right_imaginary = pointer_with_offset(context, right, half);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_val_t values[4];
  ir_val_t pointers[4] = {left, left_imaginary, right, right_imaginary};
  for (int i = 0; i < 4; i++) {
    int value = new_vreg(context);
    if (value < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(value, type.type);
    load->src1 = pointers[i];
    if (!append_instruction(context, load)) return ir_val_none();
    values[i] = load->dst;
  }
  ir_op_t compare_op = kind == PSX_HIR_EQ ? IR_FEQ : IR_FNE;
  ir_op_t combine_op = kind == PSX_HIR_EQ ? IR_AND : IR_OR;
  ir_val_t comparisons[2];
  for (int i = 0; i < 2; i++) {
    int comparison = new_vreg(context);
    if (comparison < 0) return ir_val_none();
    ir_inst_t *compare = ir_inst_new(compare_op);
    if (!compare) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    compare->dst = ir_val_vreg(comparison, IR_TY_I32);
    compare->src1 = values[i];
    compare->src2 = values[i + 2];
    if (!append_instruction(context, compare)) return ir_val_none();
    comparisons[i] = compare->dst;
  }
  return emit_integer_binary(
      context, combine_op, comparisons[0], comparisons[1], IR_TY_I32);
}

static ir_val_t load_direct_value(
    hir_ir_context_t *context, ir_val_t pointer, ir_type_t type) {
  if (pointer.type != IR_TY_PTR) return unsupported_expr(context);
  int value = new_vreg(context);
  if (value < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_LOAD);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(value, type);
  load->src1 = pointer;
  if (!append_instruction(context, load)) return ir_val_none();
  return load->dst;
}

static int store_direct_value(
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
  return append_instruction(context, store);
}

static ir_val_t emit_float_binary(
    hir_ir_context_t *context, ir_op_t op, ir_val_t left,
    ir_val_t right, ir_type_t type) {
  int result = new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *instruction = ir_inst_new(op);
  if (!instruction) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  instruction->dst = ir_val_vreg(result, type);
  instruction->src1 = left;
  instruction->src2 = right;
  if (!append_instruction(context, instruction)) return ir_val_none();
  return instruction->dst;
}

static ir_val_t materialize_complex_operand(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_abi_param_info_t target_type) {
  ir_abi_param_info_t source_type = classify_node_type(context, node);
  if (is_complex_abi_type(source_type)) {
    ir_val_t source = build_expr(context, node);
    if (context->status != IR_HIR_BUILD_OK ||
        source.type != IR_TY_PTR)
      return ir_val_none();
    if (source_type.type == target_type.type &&
        source_type.source_size == target_type.source_size)
      return source;

    int source_half = ir_type_size(source_type.type);
    int target_half = ir_type_size(target_type.type);
    int slot = allocate_scalar_temp(
        context, target_type.source_size,
        target_half >= 8 ? 8 : 4);
    if (slot < 0) return ir_val_none();
    ir_val_t destination = ir_val_vreg(slot, IR_TY_PTR);
    ir_abi_param_info_t source_component = {
        .type = source_type.type,
        .param_class = IR_ABI_PARAM_FLOAT,
        .source_size = source_half,
    };
    ir_abi_param_info_t target_component = {
        .type = target_type.type,
        .param_class = IR_ABI_PARAM_FLOAT,
        .source_size = target_half,
    };
    for (int part = 0; part < 2; part++) {
      ir_val_t source_pointer = source;
      ir_val_t destination_pointer = destination;
      if (part == 1) {
        source_pointer = pointer_with_offset(
            context, source, source_half);
        destination_pointer = pointer_with_offset(
            context, destination, target_half);
      }
      if (context->status != IR_HIR_BUILD_OK)
        return ir_val_none();
      ir_val_t component = load_direct_value(
          context, source_pointer, source_type.type);
      if (context->status == IR_HIR_BUILD_OK)
        component = coerce_direct_value(
            context, component, source_component, target_component);
      if (context->status != IR_HIR_BUILD_OK ||
          !store_direct_value(
              context, destination_pointer, component))
        return ir_val_none();
    }
    return destination;
  }
  if (!is_scalar_value_abi_type(source_type))
    return unsupported_expr(context);
  int half = ir_type_size(target_type.type);
  int slot = allocate_scalar_temp(
      context, target_type.source_size, half >= 8 ? 8 : 4);
  if (slot < 0) return ir_val_none();
  ir_val_t destination = ir_val_vreg(slot, IR_TY_PTR);
  ir_val_t real = build_expr(context, node);
  ir_abi_param_info_t component_type = {
      .type = target_type.type,
      .param_class = IR_ABI_PARAM_FLOAT,
      .source_size = half,
  };
  if (context->status == IR_HIR_BUILD_OK)
    real = coerce_direct_value(
        context, real, source_type, component_type);
  if (context->status != IR_HIR_BUILD_OK ||
      !store_direct_value(context, destination, real))
    return ir_val_none();
  int zero_vreg = new_vreg(context);
  if (zero_vreg < 0) return ir_val_none();
  ir_inst_t *zero = ir_inst_new(IR_LOAD_FP_IMM);
  if (!zero) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  zero->dst = ir_val_vreg(zero_vreg, target_type.type);
  zero->src1 = ir_val_fp_imm(target_type.type, 0.0);
  if (!append_instruction(context, zero)) return ir_val_none();
  ir_val_t imaginary = pointer_with_offset(context, destination, half);
  if (context->status != IR_HIR_BUILD_OK ||
      !store_direct_value(context, imaginary, zero->dst))
    return ir_val_none();
  return destination;
}

static ir_val_t build_complex_binary(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_abi_param_info_t type) {
  psx_hir_node_kind_t kind = psx_hir_node_kind(node);
  const psx_hir_node_t *left_node = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *right_node = child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  if (!left_node || !right_node || !is_complex_abi_type(type) ||
      (kind != PSX_HIR_ADD && kind != PSX_HIR_SUB &&
       kind != PSX_HIR_MUL && kind != PSX_HIR_DIV))
    return unsupported_expr(context);
  ir_abi_param_info_t left_type = classify_node_type(context, left_node);
  ir_abi_param_info_t right_type = classify_node_type(context, right_node);
  if ((!is_complex_abi_type(left_type) &&
       !is_scalar_value_abi_type(left_type)) ||
      (!is_complex_abi_type(right_type) &&
       !is_scalar_value_abi_type(right_type)))
    return unsupported_expr(context);
  ir_val_t left = materialize_complex_operand(
      context, left_node, type);
  ir_val_t right = materialize_complex_operand(
      context, right_node, type);
  if (context->status != IR_HIR_BUILD_OK ||
      left.type != IR_TY_PTR || right.type != IR_TY_PTR)
    return unsupported_expr(context);
  int half = ir_type_size(type.type);
  ir_val_t left_imaginary = pointer_with_offset(context, left, half);
  ir_val_t right_imaginary = pointer_with_offset(context, right, half);
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
  int slot = allocate_scalar_temp(
      context, type.source_size, half >= 8 ? 8 : 4);
  if (slot < 0) return ir_val_none();
  ir_val_t destination = ir_val_vreg(slot, IR_TY_PTR);
  ir_val_t imaginary_destination = pointer_with_offset(
      context, destination, half);
  if (context->status != IR_HIR_BUILD_OK ||
      !store_direct_value(context, destination, real) ||
      !store_direct_value(context, imaginary_destination, imaginary))
    return ir_val_none();
  return destination;
}

static ir_val_t emit_float_negate(
    hir_ir_context_t *context, ir_val_t value, ir_type_t type) {
  int result = new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *negate = ir_inst_new(IR_FNEG);
  if (!negate) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  negate->dst = ir_val_vreg(result, type);
  negate->src1 = value;
  if (!append_instruction(context, negate)) return ir_val_none();
  return negate->dst;
}

static ir_val_t build_complex_negate(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_abi_param_info_t type) {
  const psx_hir_node_t *operand = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  if (!operand || !is_complex_abi_type(type))
    return unsupported_expr(context);
  ir_val_t source = materialize_complex_operand(
      context, operand, type);
  if (context->status != IR_HIR_BUILD_OK ||
      source.type != IR_TY_PTR)
    return unsupported_expr(context);
  int half = ir_type_size(type.type);
  ir_val_t imaginary_source = pointer_with_offset(
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
  int slot = allocate_scalar_temp(
      context, type.source_size, half >= 8 ? 8 : 4);
  if (slot < 0) return ir_val_none();
  ir_val_t destination = ir_val_vreg(slot, IR_TY_PTR);
  ir_val_t imaginary_destination = pointer_with_offset(
      context, destination, half);
  if (context->status != IR_HIR_BUILD_OK ||
      !store_direct_value(context, destination, real) ||
      !store_direct_value(
          context, imaginary_destination, imaginary))
    return ir_val_none();
  return destination;
}

static ir_val_t build_complex_component(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_abi_param_info_t result_type) {
  const psx_hir_node_t *operand = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  int is_real = psx_hir_node_kind(node) == PSX_HIR_CREAL;
  if (!operand || !is_float_abi_type(result_type))
    return unsupported_expr(context);
  ir_abi_param_info_t operand_type = classify_node_type(
      context, operand);
  if (!is_complex_abi_type(operand_type)) {
    if (!is_real) {
      int zero_vreg = new_vreg(context);
      if (zero_vreg < 0) return ir_val_none();
      ir_inst_t *zero = ir_inst_new(IR_LOAD_FP_IMM);
      if (!zero) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return ir_val_none();
      }
      zero->dst = ir_val_vreg(zero_vreg, result_type.type);
      zero->src1 = ir_val_fp_imm(result_type.type, 0.0);
      if (!append_instruction(context, zero)) return ir_val_none();
      return zero->dst;
    }
    ir_val_t value = build_expr(context, operand);
    if (context->status == IR_HIR_BUILD_OK)
      value = coerce_direct_value(
          context, value, operand_type, result_type);
    return value;
  }
  if (operand_type.type != result_type.type)
    return unsupported_expr(context);
  ir_val_t value = build_expr(context, operand);
  if (context->status != IR_HIR_BUILD_OK ||
      value.type != IR_TY_PTR)
    return unsupported_expr(context);
  if (!is_real)
    value = pointer_with_offset(
        context, value, ir_type_size(result_type.type));
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  return load_direct_value(context, value, result_type.type);
}

static ir_val_t build_scalar_negate(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_abi_param_info_t type) {
  const psx_hir_node_t *operand = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  if (!operand || !is_direct_value_abi_type(type))
    return unsupported_expr(context);
  ir_val_t value = build_expr(context, operand);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  value = coerce_direct_value(
      context, value, classify_node_type(context, operand), type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  if (is_float_abi_type(type))
    return emit_float_negate(context, value, type.type);
  if (type.param_class != IR_ABI_PARAM_INTEGER)
    return unsupported_expr(context);
  return emit_integer_binary(
      context, IR_SUB, ir_val_imm(type.type, 0), value, type.type);
}

static ir_val_t emit_integer_width_conversion(
    hir_ir_context_t *context, ir_val_t value, ir_type_t target,
    int sign_extend) {
  if (!is_integer_ir_type(value.type) || !is_integer_ir_type(target))
    return unsupported_expr(context);
  if (value.type == target) return value;
  if (value.id == IR_VAL_IMM) {
    value.type = target;
    return value;
  }
  int result = new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *conversion = ir_inst_new(
      ir_type_size(value.type) > ir_type_size(target)
          ? IR_TRUNC : sign_extend ? IR_SEXT : IR_ZEXT);
  if (!conversion) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  conversion->dst = ir_val_vreg(result, target);
  conversion->src1 = value;
  if (!append_instruction(context, conversion)) return ir_val_none();
  return conversion->dst;
}

static ir_val_t emit_integer_binary(
    hir_ir_context_t *context, ir_op_t op, ir_val_t left,
    ir_val_t right, ir_type_t type) {
  int result = new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *instruction = ir_inst_new(op);
  if (!instruction) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  instruction->dst = ir_val_vreg(result, type);
  instruction->src1 = left;
  instruction->src2 = right;
  if (!append_instruction(context, instruction)) return ir_val_none();
  return instruction->dst;
}

static ir_val_t bitfield_constant(
    hir_ir_context_t *context, ir_type_t type, uint64_t value) {
  if (type != IR_TY_I64)
    return ir_val_imm(type, (long long)value);
  int result = new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_LOAD_IMM);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(result, type);
  load->src1 = ir_val_imm(type, (long long)value);
  if (!append_instruction(context, load)) return ir_val_none();
  return load->dst;
}

static int valid_bitfield(int bit_width, int bit_offset) {
  return bit_width > 0 && bit_width <= 64 && bit_offset >= 0 &&
         bit_offset < 64 && bit_width + bit_offset <= 64;
}

static ir_val_t emit_bitfield_load(
    hir_ir_context_t *context, ir_val_t pointer, int bit_width,
    int bit_offset, int is_signed, ir_type_t result_type) {
  if (!valid_bitfield(bit_width, bit_offset) ||
      !is_integer_ir_type(result_type))
    return unsupported_expr(context);
  ir_type_t unit_type = bit_width + bit_offset > 32
                            ? IR_TY_I64 : IR_TY_I32;
  int loaded_vreg = new_vreg(context);
  if (loaded_vreg < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_LOAD);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(loaded_vreg, unit_type);
  load->src1 = pointer;
  if (!append_instruction(context, load)) return ir_val_none();
  ir_val_t current = load->dst;
  if (bit_offset > 0) {
    current = emit_integer_binary(
        context, IR_SHR, current,
        ir_val_imm(unit_type, bit_offset), unit_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  }
  uint64_t mask = bit_width == 64
                      ? UINT64_MAX : (UINT64_C(1) << bit_width) - 1;
  current = emit_integer_binary(
      context, IR_AND, current,
      bitfield_constant(context, unit_type, mask), unit_type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  if (is_signed && bit_width < (unit_type == IR_TY_I64 ? 64 : 32)) {
    uint64_t sign_bit = UINT64_C(1) << (bit_width - 1);
    current = emit_integer_binary(
        context, IR_XOR, current,
        bitfield_constant(context, unit_type, sign_bit), unit_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    current = emit_integer_binary(
        context, IR_SUB, current,
        bitfield_constant(context, unit_type, sign_bit), unit_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  }
  return emit_integer_width_conversion(
      context, current, result_type, is_signed);
}

static ir_val_t emit_bitfield_store(
    hir_ir_context_t *context, ir_val_t pointer, ir_val_t value,
    int bit_width, int bit_offset) {
  if (!valid_bitfield(bit_width, bit_offset) ||
      !is_integer_ir_type(value.type))
    return unsupported_expr(context);
  ir_type_t unit_type = bit_width + bit_offset > 32
                            ? IR_TY_I64 : IR_TY_I32;
  ir_val_t stored_value = emit_integer_width_conversion(
      context, value, unit_type, 0);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  int loaded_vreg = new_vreg(context);
  if (loaded_vreg < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_LOAD);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(loaded_vreg, unit_type);
  load->src1 = pointer;
  if (!append_instruction(context, load)) return ir_val_none();
  uint64_t mask = bit_width == 64
                      ? UINT64_MAX : (UINT64_C(1) << bit_width) - 1;
  uint64_t shifted_mask = mask << bit_offset;
  ir_val_t cleared = emit_integer_binary(
      context, IR_AND, load->dst,
      bitfield_constant(context, unit_type, ~shifted_mask), unit_type);
  ir_val_t field = emit_integer_binary(
      context, IR_AND, stored_value,
      bitfield_constant(context, unit_type, mask), unit_type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  if (bit_offset > 0) {
    field = emit_integer_binary(
        context, IR_SHL, field,
        ir_val_imm(unit_type, bit_offset), unit_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  }
  ir_val_t combined = emit_integer_binary(
      context, IR_OR, cleared, field, unit_type);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_inst_t *store = ir_inst_new(IR_STORE);
  if (!store) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  store->src1 = pointer;
  store->src2 = combined;
  if (!append_instruction(context, store)) return ir_val_none();
  return value;
}

static int allocate_scalar_temp(
    hir_ir_context_t *context, int size, int alignment) {
  if (size <= 0 || alignment <= 0 || alignment > 16) {
    context->status = IR_HIR_BUILD_UNSUPPORTED;
    return -1;
  }
  int pointer = new_vreg(context);
  if (pointer < 0) return -1;
  ir_inst_t *alloca = ir_inst_new(IR_ALLOCA);
  if (!alloca) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return -1;
  }
  alloca->dst = ir_val_vreg(pointer, IR_TY_PTR);
  alloca->alloca_size = size;
  alloca->alloca_align = alignment;
  if (!append_instruction(context, alloca)) return -1;
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
  return append_instruction(context, store);
}

static ir_val_t load_scalar_temp(
    hir_ir_context_t *context, int pointer, ir_type_t type,
    int is_unsigned) {
  int result = new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_LOAD);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(result, type);
  load->src1 = ir_val_vreg(pointer, IR_TY_PTR);
  load->is_unsigned = is_unsigned ? 1 : 0;
  if (!append_instruction(context, load)) return ir_val_none();
  return load->dst;
}

static ir_val_t scalar_truth_value(
    hir_ir_context_t *context, ir_val_t value) {
  if (is_float_ir_type(value.type)) {
    int zero_vreg = new_vreg(context);
    if (zero_vreg < 0) return ir_val_none();
    ir_inst_t *zero = ir_inst_new(IR_LOAD_FP_IMM);
    if (!zero) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    zero->dst = ir_val_vreg(zero_vreg, value.type);
    zero->src1 = ir_val_fp_imm(value.type, 0.0);
    if (!append_instruction(context, zero)) return ir_val_none();
    int result = new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *compare = ir_inst_new(IR_FNE);
    if (!compare) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    compare->dst = ir_val_vreg(result, IR_TY_I32);
    compare->src1 = value;
    compare->src2 = zero->dst;
    if (!append_instruction(context, compare)) return ir_val_none();
    return compare->dst;
  }
  if (!is_integer_ir_type(value.type) && value.type != IR_TY_PTR)
    return unsupported_expr(context);
  int result = new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *compare = ir_inst_new(IR_NE);
  if (!compare) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  compare->dst = ir_val_vreg(result, IR_TY_I32);
  compare->src1 = value;
  compare->src2 = ir_val_imm(value.type, 0);
  if (!append_instruction(context, compare)) return ir_val_none();
  return compare->dst;
}

static ir_val_t build_inc_dec(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_abi_param_info_t type) {
  const psx_hir_node_t *target = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  if (!target ||
      (psx_hir_node_kind(target) != PSX_HIR_LOCAL &&
       psx_hir_node_kind(target) != PSX_HIR_DEREF &&
       psx_hir_node_kind(target) != PSX_HIR_GLOBAL))
    return unsupported_expr(context);
  if (type.param_class != IR_ABI_PARAM_INTEGER &&
      type.param_class != IR_ABI_PARAM_POINTER &&
      !is_float_abi_type(type))
    return unsupported_expr(context);

  ir_val_t pointer = lvalue_address(context, target);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_type_t value_type = scalar_storage_type(type);
  int bit_width = 0;
  int bit_offset = 0;
  int bit_is_signed = 0;
  int is_bitfield = psx_hir_node_bitfield_info(
      target, &bit_width, &bit_offset, &bit_is_signed);
  ir_val_t old_value;
  if (is_bitfield) {
    old_value = emit_bitfield_load(
        context, pointer, bit_width, bit_offset, bit_is_signed,
        value_type);
  } else {
    int old_vreg = new_vreg(context);
    if (old_vreg < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(old_vreg, value_type);
    load->src1 = pointer;
    load->is_unsigned = type.is_unsigned ? 1 : 0;
    if (!append_instruction(context, load)) return ir_val_none();
    old_value = load->dst;
  }
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();

  long long step = 1;
  ir_val_t step_value = ir_val_none();
  if (type.param_class == IR_ABI_PARAM_POINTER) {
    int stride_offset = psx_hir_node_vla_stride_frame_offset(target);
    if (stride_offset != 0) {
      int slot_size = psx_hir_node_vla_stride_slot_size(target);
      if (slot_size <= 0) slot_size = 8;
      int stride_pointer = local_storage_address(
          context, stride_offset, slot_size, slot_size >= 8 ? 8 : 4);
      if (stride_pointer < 0) return ir_val_none();
      int stride_vreg = new_vreg(context);
      if (stride_vreg < 0) return ir_val_none();
      ir_inst_t *load_stride = ir_inst_new(IR_LOAD);
      if (!load_stride) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return ir_val_none();
      }
      load_stride->dst = ir_val_vreg(stride_vreg, value_type);
      load_stride->src1 = ir_val_vreg(stride_pointer, IR_TY_PTR);
      if (!append_instruction(context, load_stride)) return ir_val_none();
      step_value = load_stride->dst;
    } else {
      psx_qual_type_t pointee = psx_semantic_type_table_pointee_value(
          context->options->semantic_types,
          psx_hir_node_qual_type(target).type_id);
      step = ps_type_sizeof_id_with_records(
          context->options->semantic_types,
          context->options->record_layouts,
          pointee.type_id, context->options->target);
      if (step <= 0) return unsupported_expr(context);
    }
  }
  psx_hir_node_kind_t kind = psx_hir_node_kind(node);
  int is_increment = kind == PSX_HIR_PRE_INC || kind == PSX_HIR_POST_INC;
  ir_val_t new_value;
  if (is_float_abi_type(type)) {
    int one_vreg = new_vreg(context);
    if (one_vreg < 0) return ir_val_none();
    ir_inst_t *one = ir_inst_new(IR_LOAD_FP_IMM);
    if (!one) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    one->dst = ir_val_vreg(one_vreg, value_type);
    one->src1 = ir_val_fp_imm(value_type, 1.0);
    if (!append_instruction(context, one)) return ir_val_none();
    new_value = emit_float_binary(
        context, is_increment ? IR_FADD : IR_FSUB,
        old_value, one->dst, value_type);
  } else {
    new_value = emit_integer_binary(
        context, is_increment ? IR_ADD : IR_SUB, old_value,
        step_value.id == IR_VAL_NONE
            ? ir_val_imm(value_type, step) : step_value,
        value_type);
  }
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  if (is_bitfield) {
    (void)emit_bitfield_store(
        context, pointer, new_value, bit_width, bit_offset);
  } else {
    ir_inst_t *store = ir_inst_new(IR_STORE);
    if (!store) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    store->src1 = pointer;
    store->src2 = new_value;
    if (!append_instruction(context, store)) return ir_val_none();
  }
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  return kind == PSX_HIR_PRE_INC || kind == PSX_HIR_PRE_DEC
             ? new_value : old_value;
}

static ir_val_t build_short_circuit(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    int is_and) {
  const psx_hir_node_t *left = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *right = child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  if (!left || !right) return unsupported_expr(context);
  int slot = allocate_scalar_temp(context, 4, 4);
  if (slot < 0 ||
      !store_scalar_temp(
          context, slot, ir_val_imm(IR_TY_I32, is_and ? 0 : 1)))
    return ir_val_none();
  ir_val_t left_value = build_expr(context, left);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_block_t *right_block = new_block(context);
  ir_block_t *merge_block = new_block(context);
  if (!right_block || !merge_block ||
      !emit_conditional_branch(
          context, left_value,
          is_and ? right_block : merge_block,
          is_and ? merge_block : right_block) ||
      !switch_to_block(context, right_block))
    return ir_val_none();
  ir_val_t right_value = build_expr(context, right);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  right_value = scalar_truth_value(context, right_value);
  if (context->status != IR_HIR_BUILD_OK ||
      !store_scalar_temp(context, slot, right_value) ||
      !emit_branch(context, merge_block) ||
      !switch_to_block(context, merge_block))
    return ir_val_none();
  return load_scalar_temp(context, slot, IR_TY_I32, 0);
}

static ir_val_t build_scalar_ternary(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_abi_param_info_t result_type) {
  const psx_hir_node_t *condition = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *if_true = child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  const psx_hir_node_t *if_false = child_for_edge(
      context, node, PSX_HIR_EDGE_ELSE, 0);
  if (!condition || !if_true || !if_false ||
      !is_direct_value_abi_type(result_type))
    return unsupported_expr(context);
  int size = result_type.source_size;
  int alignment = size >= 8 ? 8 : size >= 4 ? 4 :
                  size >= 2 ? 2 : 1;
  int slot = allocate_scalar_temp(context, size, alignment);
  if (slot < 0) return ir_val_none();
  ir_val_t condition_value = build_expr(context, condition);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_block_t *true_block = new_block(context);
  ir_block_t *false_block = new_block(context);
  ir_block_t *merge_block = new_block(context);
  if (!true_block || !false_block || !merge_block ||
      !emit_conditional_branch(
          context, condition_value, true_block, false_block) ||
      !switch_to_block(context, true_block))
    return ir_val_none();
  ir_val_t true_value = build_expr(context, if_true);
  if (context->status == IR_HIR_BUILD_OK)
    true_value = coerce_direct_value(
        context, true_value, classify_node_type(context, if_true),
        result_type);
  if (context->status != IR_HIR_BUILD_OK ||
      !store_scalar_temp(context, slot, true_value) ||
      !emit_branch(context, merge_block) ||
      !switch_to_block(context, false_block))
    return ir_val_none();
  ir_val_t false_value = build_expr(context, if_false);
  if (context->status == IR_HIR_BUILD_OK)
    false_value = coerce_direct_value(
        context, false_value, classify_node_type(context, if_false),
        result_type);
  if (context->status != IR_HIR_BUILD_OK ||
      !store_scalar_temp(context, slot, false_value) ||
      !emit_branch(context, merge_block) ||
      !switch_to_block(context, merge_block))
    return ir_val_none();
  return load_scalar_temp(
      context, slot, scalar_storage_type(result_type),
      result_type.is_unsigned);
}

static ir_val_t build_void_ternary(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  const psx_hir_node_t *condition = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *if_true = child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  const psx_hir_node_t *if_false = child_for_edge(
      context, node, PSX_HIR_EDGE_ELSE, 0);
  if (!condition || !if_true || !if_false)
    return unsupported_expr(context);
  ir_val_t condition_value = build_expr(context, condition);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_block_t *true_block = new_block(context);
  ir_block_t *false_block = new_block(context);
  ir_block_t *merge_block = new_block(context);
  if (!true_block || !false_block || !merge_block ||
      !emit_conditional_branch(
          context, condition_value, true_block, false_block) ||
      !switch_to_block(context, true_block))
    return ir_val_none();
  (void)build_expr(context, if_true);
  if (context->status != IR_HIR_BUILD_OK ||
      !emit_branch(context, merge_block) ||
      !switch_to_block(context, false_block))
    return ir_val_none();
  (void)build_expr(context, if_false);
  if (context->status != IR_HIR_BUILD_OK ||
      !emit_branch(context, merge_block) ||
      !switch_to_block(context, merge_block))
    return ir_val_none();
  return ir_val_none();
}

static psx_qual_type_t atomic_pointee_type(
    const hir_ir_context_t *context,
    const psx_hir_node_t *pointer_argument) {
  const psx_hir_node_t *current = pointer_argument;
  while (current) {
    psx_qual_type_t pointer_type = psx_hir_node_qual_type(current);
    const psx_type_t *pointer = psx_semantic_type_table_lookup(
        context->options->semantic_types, pointer_type.type_id);
    if (pointer && (pointer->kind == PSX_TYPE_POINTER ||
                    pointer->kind == PSX_TYPE_ARRAY)) {
      psx_qual_type_t pointee = psx_semantic_type_table_base(
          context->options->semantic_types, pointer_type.type_id);
      const psx_type_t *pointee_type = psx_semantic_type_table_lookup(
          context->options->semantic_types, pointee.type_id);
      if (pointee_type && pointee_type->kind != PSX_TYPE_VOID)
        return pointee;
    }
    if (psx_hir_node_kind(current) != PSX_HIR_CAST) break;
    current = child_for_edge(
        context, current, PSX_HIR_EDGE_LHS, 0);
  }
  return (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
}

static int atomic_operation_width(
    const hir_ir_context_t *context,
    const psx_hir_node_t *pointer_argument, int *is_unsigned) {
  psx_qual_type_t pointee = atomic_pointee_type(
      context, pointer_argument);
  const psx_type_t *pointee_type = psx_semantic_type_table_lookup(
      context->options->semantic_types, pointee.type_id);
  int width = ps_type_sizeof_id_with_records(
      context->options->semantic_types,
      context->options->record_layouts,
      pointee.type_id, context->options->target);
  if (!pointee_type ||
      (pointee_type->kind != PSX_TYPE_BOOL &&
       pointee_type->kind != PSX_TYPE_INTEGER &&
       pointee_type->kind != PSX_TYPE_POINTER) ||
      (width != 1 && width != 2 && width != 4 && width != 8)) {
    return 0;
  }
  if (is_unsigned)
    *is_unsigned = ps_type_is_unsigned(pointee_type) ? 1 : 0;
  return width;
}

static ir_val_t build_atomic_call(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    const char *name, size_t name_length) {
  static const size_t prefix_length = 12;
  const char *suffix = name + prefix_length;
  size_t suffix_length = name_length - prefix_length;
  size_t argument_count = child_count_for_edge(
      node, PSX_HIR_EDGE_ARGUMENT);

  if (suffix_length == 5 && memcmp(suffix, "fence", 5) == 0) {
    if (argument_count != 0) return unsupported_expr(context);
    ir_inst_t *atomic = ir_inst_new(IR_ATOMIC);
    if (!atomic) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    atomic->atomic_kind = IR_ATOMIC_FENCE;
    if (!append_instruction(context, atomic)) return ir_val_none();
    return ir_val_imm(IR_TY_I32, 0);
  }

  const psx_hir_node_t *pointer_argument = child_for_edge(
      context, node, PSX_HIR_EDGE_ARGUMENT, 0);
  int is_unsigned = 0;
  int width = atomic_operation_width(
      context, pointer_argument, &is_unsigned);
  if (!pointer_argument || width == 0)
    return unsupported_expr(context);
  ir_val_t pointer = build_expr(context, pointer_argument);
  if (context->status != IR_HIR_BUILD_OK ||
      pointer.type != IR_TY_PTR)
    return unsupported_expr(context);
  ir_type_t value_type = width == 8 ? IR_TY_I64 : IR_TY_I32;

  if (suffix_length == 4 && memcmp(suffix, "load", 4) == 0) {
    if (argument_count != 1) return unsupported_expr(context);
    int result = new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *atomic = ir_inst_new(IR_ATOMIC);
    if (!atomic) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    atomic->atomic_kind = IR_ATOMIC_LOAD;
    atomic->atomic_width = (unsigned char)width;
    atomic->is_unsigned = (unsigned char)is_unsigned;
    atomic->src1 = pointer;
    atomic->dst = ir_val_vreg(result, value_type);
    if (!append_instruction(context, atomic)) return ir_val_none();
    return atomic->dst;
  }

  if (suffix_length == 5 && memcmp(suffix, "store", 5) == 0) {
    if (argument_count != 2) return unsupported_expr(context);
    const psx_hir_node_t *value_node = child_for_edge(
        context, node, PSX_HIR_EDGE_ARGUMENT, 1);
    ir_val_t value = build_expr(context, value_node);
    if (context->status != IR_HIR_BUILD_OK ||
        !is_integer_ir_type(value.type))
      return unsupported_expr(context);
    ir_inst_t *atomic = ir_inst_new(IR_ATOMIC);
    if (!atomic) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    atomic->atomic_kind = IR_ATOMIC_STORE;
    atomic->atomic_width = (unsigned char)width;
    atomic->src1 = pointer;
    atomic->src2 = value;
    if (!append_instruction(context, atomic)) return ir_val_none();
    return ir_val_imm(IR_TY_I32, 0);
  }

  if (suffix_length == 3 && memcmp(suffix, "cas", 3) == 0) {
    if (argument_count != 3) return unsupported_expr(context);
    const psx_hir_node_t *expected_node = child_for_edge(
        context, node, PSX_HIR_EDGE_ARGUMENT, 1);
    const psx_hir_node_t *desired_node = child_for_edge(
        context, node, PSX_HIR_EDGE_ARGUMENT, 2);
    ir_val_t expected = build_expr(context, expected_node);
    ir_val_t desired = build_expr(context, desired_node);
    if (context->status != IR_HIR_BUILD_OK ||
        expected.type != IR_TY_PTR ||
        !is_integer_ir_type(desired.type))
      return unsupported_expr(context);
    int result = new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *atomic = ir_inst_new(IR_ATOMIC);
    if (!atomic) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    atomic->atomic_kind = IR_ATOMIC_CAS;
    atomic->atomic_width = (unsigned char)width;
    atomic->is_unsigned = (unsigned char)is_unsigned;
    atomic->src1 = pointer;
    atomic->src2 = expected;
    atomic->src3 = desired;
    atomic->dst = ir_val_vreg(result, IR_TY_I32);
    if (!append_instruction(context, atomic)) return ir_val_none();
    return atomic->dst;
  }

  int rmw_operation = -1;
  if (suffix_length == 8 &&
      memcmp(suffix, "exchange", 8) == 0)
    rmw_operation = IR_ARMW_XCHG;
  else if (suffix_length == 9 &&
           memcmp(suffix, "fetch_add", 9) == 0)
    rmw_operation = IR_ARMW_ADD;
  else if (suffix_length == 9 &&
           memcmp(suffix, "fetch_sub", 9) == 0)
    rmw_operation = IR_ARMW_SUB;
  else if (suffix_length == 8 &&
           memcmp(suffix, "fetch_or", 8) == 0)
    rmw_operation = IR_ARMW_OR;
  else if (suffix_length == 9 &&
           memcmp(suffix, "fetch_and", 9) == 0)
    rmw_operation = IR_ARMW_AND;
  else if (suffix_length == 9 &&
           memcmp(suffix, "fetch_xor", 9) == 0)
    rmw_operation = IR_ARMW_XOR;
  if (rmw_operation < 0 || argument_count != 2)
    return unsupported_expr(context);

  const psx_hir_node_t *value_node = child_for_edge(
      context, node, PSX_HIR_EDGE_ARGUMENT, 1);
  ir_val_t value = build_expr(context, value_node);
  if (context->status != IR_HIR_BUILD_OK ||
      !is_integer_ir_type(value.type))
    return unsupported_expr(context);
  int result = new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *atomic = ir_inst_new(IR_ATOMIC);
  if (!atomic) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  atomic->atomic_kind = IR_ATOMIC_RMW;
  atomic->atomic_rmw_op = (unsigned char)rmw_operation;
  atomic->atomic_width = (unsigned char)width;
  atomic->is_unsigned = (unsigned char)is_unsigned;
  atomic->src1 = pointer;
  atomic->src2 = value;
  atomic->dst = ir_val_vreg(result, value_type);
  if (!append_instruction(context, atomic)) return ir_val_none();
  return atomic->dst;
}

static ir_val_t build_scalar_or_void_call(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_abi_param_info_t result_type);

static ir_val_t aggregate_value_address(
    hir_ir_context_t *context, const psx_hir_node_t *node);

static ir_val_t build_aggregate_assignment(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_abi_param_info_t target_type) {
  const psx_hir_node_t *target = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *source_node = child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  ir_abi_param_info_t source_type = classify_node_type(
      context, source_node);
  if (!target || !source_node ||
      target_type.param_class != IR_ABI_PARAM_AGGREGATE ||
      source_type.param_class != IR_ABI_PARAM_AGGREGATE ||
      target_type.source_size <= 0 ||
      source_type.source_size != target_type.source_size)
    return unsupported_expr(context);
  ir_val_t destination = lvalue_address(context, target);
  ir_val_t source = aggregate_value_address(context, source_node);
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
  if (!append_instruction(context, copy)) return ir_val_none();
  return destination;
}

static int copy_aggregate_value_to(
    hir_ir_context_t *context, const psx_hir_node_t *source_node,
    ir_val_t destination, ir_abi_param_info_t result_type) {
  ir_abi_param_info_t source_type = classify_node_type(
      context, source_node);
  if (destination.type != IR_TY_PTR ||
      result_type.param_class != IR_ABI_PARAM_AGGREGATE ||
      source_type.param_class != IR_ABI_PARAM_AGGREGATE ||
      result_type.source_size <= 0 ||
      source_type.source_size != result_type.source_size) {
    unsupported_expr(context);
    return 0;
  }
  ir_val_t source = aggregate_value_address(context, source_node);
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
  return append_instruction(context, copy);
}

static ir_val_t build_aggregate_ternary_address(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_abi_param_info_t result_type) {
  const psx_hir_node_t *condition = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *if_true = child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  const psx_hir_node_t *if_false = child_for_edge(
      context, node, PSX_HIR_EDGE_ELSE, 0);
  if (!condition || !if_true || !if_false ||
      !is_indirect_aggregate_abi_type(result_type))
    return unsupported_expr(context);
  int temporary = allocate_scalar_temp(
      context, result_type.source_size,
      result_type.source_size >= 8 ? 8 : 4);
  if (temporary < 0) return ir_val_none();
  ir_val_t destination = ir_val_vreg(temporary, IR_TY_PTR);
  ir_val_t condition_value = build_expr(context, condition);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  ir_block_t *true_block = new_block(context);
  ir_block_t *false_block = new_block(context);
  ir_block_t *merge_block = new_block(context);
  if (!true_block || !false_block || !merge_block ||
      !emit_conditional_branch(
          context, condition_value, true_block, false_block) ||
      !switch_to_block(context, true_block) ||
      !copy_aggregate_value_to(
          context, if_true, destination, result_type) ||
      !emit_branch(context, merge_block) ||
      !switch_to_block(context, false_block) ||
      !copy_aggregate_value_to(
          context, if_false, destination, result_type) ||
      !emit_branch(context, merge_block) ||
      !switch_to_block(context, merge_block))
    return ir_val_none();
  return destination;
}

static ir_val_t aggregate_value_address(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  if (!node) return unsupported_expr(context);
  psx_hir_node_kind_t kind = psx_hir_node_kind(node);
  if (kind == PSX_HIR_LOCAL || kind == PSX_HIR_GLOBAL ||
      kind == PSX_HIR_DEREF)
    return lvalue_address(context, node);
  if (kind == PSX_HIR_CALL)
    return build_scalar_or_void_call(
        context, node, classify_node_type(context, node));
  if (kind == PSX_HIR_ASSIGN)
    return build_aggregate_assignment(
        context, node, classify_node_type(context, node));
  if (kind == PSX_HIR_TERNARY)
    return build_aggregate_ternary_address(
        context, node, classify_node_type(context, node));
  if (kind == PSX_HIR_COMMA) {
    const psx_hir_node_t *left = child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    const psx_hir_node_t *right = child_for_edge(
        context, node, PSX_HIR_EDGE_RHS, 0);
    if (!left || !right) return unsupported_expr(context);
    (void)build_expr(context, left);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    return aggregate_value_address(context, right);
  }
  return unsupported_expr(context);
}

static ir_val_t build_scalar_or_void_call(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_abi_param_info_t result_type) {
  size_t name_length = 0;
  const char *name = psx_hir_node_name(node, &name_length);
  const psx_hir_node_t *callee = child_for_edge(
      context, node, PSX_HIR_EDGE_CALLEE, 0);
  int is_direct = name && name_length > 0;
  if (name_length > INT_MAX || is_direct == (callee != NULL))
    return unsupported_expr(context);
  if (is_direct && name_length > 12 &&
      memcmp(name, "__ag_atomic_", 12) == 0)
    return build_atomic_call(context, node, name, name_length);
  psx_qual_type_t callable_type =
      psx_semantic_type_table_callable_function(
          context->options->semantic_types,
          psx_hir_node_attached_qual_type(node));
  ir_abi_type_context_t abi = {
      .semantic_types = context->options->semantic_types,
      .record_layouts = context->options->record_layouts,
      .target = context->options->target,
  };
  ir_callable_sig_t signature;
  size_t argument_count = child_count_for_edge(
      node, PSX_HIR_EDGE_ARGUMENT);
  int accepts_unprototyped_arguments = 0;
  int is_void_result =
      node_type_kind(context, node) == PSX_TYPE_VOID;
  int is_complex_result = is_complex_abi_type(result_type);
  int is_indirect_aggregate_result =
      is_indirect_aggregate_abi_type(result_type);
  if (callable_type.type_id == PSX_TYPE_ID_INVALID ||
      !ir_abi_callable_sig_from_type_id(
          &abi, callable_type.type_id, &signature) ||
      argument_count > INT_MAX ||
      (signature.is_variadic
           ? argument_count < signature.param_count
           : argument_count != signature.param_count &&
                 !(signature.param_count == 0 && argument_count > 0)) ||
      (signature.is_variadic && signature.param_count > 8) ||
      signature.result != (is_void_result ? IR_TY_VOID : result_type.type) ||
      (!is_void_result && !is_complex_result &&
       !is_indirect_aggregate_result &&
       !is_direct_value_abi_type(result_type)))
    return unsupported_expr(context);
  accepts_unprototyped_arguments =
      !signature.is_variadic && signature.param_count == 0 &&
      argument_count > 0;

  ir_val_t callee_value = ir_val_none();
  if (callee) {
    ir_abi_param_info_t callee_type = classify_node_type(context, callee);
    callee_value = build_expr(context, callee);
    if (context->status != IR_HIR_BUILD_OK ||
        callee_type.param_class != IR_ABI_PARAM_POINTER ||
        callee_value.type != IR_TY_PTR)
      return unsupported_expr(context);
  }

  ir_val_t *arguments = NULL;
  ir_type_t *argument_abi_types = NULL;
  size_t argument_capacity = 0;
  size_t emitted_count = 0;
  size_t emitted_fixed_count = 0;
  for (size_t i = 0; i < argument_count; i++) {
    const psx_hir_node_t *argument = child_for_edge(
        context, node, PSX_HIR_EDGE_ARGUMENT, i);
    ir_abi_param_info_t argument_type = classify_node_type(
        context, argument);
    size_t slots = is_complex_abi_type(argument_type) ? 2 : 1;
    if (i >= signature.param_count &&
        argument_type.param_class == IR_ABI_PARAM_AGGREGATE) {
      if (argument_type.source_size <= 0 ||
          argument_type.source_size > INT_MAX - 7)
        return unsupported_expr(context);
      slots = (size_t)((argument_type.source_size + 7) / 8);
    }
    if (slots > SIZE_MAX - argument_capacity)
      return unsupported_expr(context);
    argument_capacity += slots;
  }
  if (argument_count) {
    arguments = calloc(argument_capacity, sizeof(*arguments));
    argument_abi_types = calloc(
        argument_capacity, sizeof(*argument_abi_types));
    if (!arguments || !argument_abi_types) {
      free(arguments);
      free(argument_abi_types);
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
  }
  for (size_t i = 0; i < argument_count; i++) {
    const psx_hir_node_t *argument = child_for_edge(
        context, node, PSX_HIR_EDGE_ARGUMENT, i);
    ir_abi_param_info_t argument_type = classify_node_type(
        context, argument);
    ir_abi_param_info_t parameter_type;
    if (i < signature.param_count) {
      psx_qual_type_t parameter_type_id =
          psx_semantic_type_table_parameter(
              context->options->semantic_types,
              callable_type.type_id, (int)i);
      parameter_type = ir_abi_classify_type_id(
          &abi, parameter_type_id.type_id);
    } else {
      parameter_type = argument_type;
      if (argument_type.param_class == IR_ABI_PARAM_INTEGER &&
          argument_type.source_size < 4) {
        const psx_type_t *semantic_type =
            psx_semantic_type_table_lookup(
                context->options->semantic_types,
                psx_hir_node_qual_type(argument).type_id);
        parameter_type.type = IR_TY_I32;
        parameter_type.source_size = 4;
        parameter_type.is_unsigned =
            ps_type_integer_promotion_is_unsigned_for_target(
                semantic_type, context->options->target);
      }
    }
    if (i >= signature.param_count &&
        argument_type.param_class == IR_ABI_PARAM_AGGREGATE) {
      int rounded_size =
          ((argument_type.source_size + 7) / 8) * 8;
      size_t chunk_count = (size_t)(rounded_size / 8);
      if (emitted_count > argument_capacity - chunk_count) {
        free(arguments);
        free(argument_abi_types);
        return unsupported_expr(context);
      }
      ir_val_t source = aggregate_value_address(context, argument);
      int temporary = allocate_scalar_temp(context, rounded_size, 8);
      if (context->status != IR_HIR_BUILD_OK ||
          source.type != IR_TY_PTR || temporary < 0) {
        free(arguments);
        free(argument_abi_types);
        return ir_val_none();
      }
      ir_inst_t *copy = ir_inst_new(IR_MEMCPY);
      if (!copy) {
        free(arguments);
        free(argument_abi_types);
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return ir_val_none();
      }
      copy->src1 = ir_val_vreg(temporary, IR_TY_PTR);
      copy->src2 = source;
      copy->alloca_size = argument_type.source_size;
      if (!append_instruction(context, copy)) {
        free(arguments);
        free(argument_abi_types);
        return ir_val_none();
      }
      for (int offset = 0; offset < rounded_size; offset += 8) {
        ir_val_t chunk_pointer = ir_val_vreg(temporary, IR_TY_PTR);
        if (offset > 0)
          chunk_pointer = pointer_with_offset(
              context, chunk_pointer, offset);
        ir_val_t chunk = load_direct_value(
            context, chunk_pointer, IR_TY_I64);
        if (context->status != IR_HIR_BUILD_OK) {
          free(arguments);
          free(argument_abi_types);
          return ir_val_none();
        }
        arguments[emitted_count] = chunk;
        argument_abi_types[emitted_count++] = IR_TY_I64;
      }
      continue;
    }
    if (i < signature.param_count &&
        is_complex_abi_type(parameter_type)) {
      if (!is_complex_abi_type(argument_type) ||
          emitted_count + 2 > argument_capacity) {
        free(arguments);
        free(argument_abi_types);
        return unsupported_expr(context);
      }
      ir_val_t pointer = materialize_complex_operand(
          context, argument, parameter_type);
      if (context->status != IR_HIR_BUILD_OK ||
          pointer.type != IR_TY_PTR) {
        free(arguments);
        free(argument_abi_types);
        return ir_val_none();
      }
      ir_val_t imaginary_pointer = pointer_with_offset(
          context, pointer, ir_type_size(parameter_type.type));
      ir_val_t real = load_direct_value(
          context, pointer, parameter_type.type);
      ir_val_t imaginary = load_direct_value(
          context, imaginary_pointer, parameter_type.type);
      if (context->status != IR_HIR_BUILD_OK) {
        free(arguments);
        free(argument_abi_types);
        return ir_val_none();
      }
      arguments[emitted_count] = real;
      argument_abi_types[emitted_count++] = parameter_type.type;
      arguments[emitted_count] = imaginary;
      argument_abi_types[emitted_count++] = parameter_type.type;
      emitted_fixed_count = emitted_count;
      continue;
    }
    if (i < signature.param_count &&
        is_indirect_aggregate_abi_type(parameter_type)) {
      if (argument_type.param_class != IR_ABI_PARAM_AGGREGATE ||
          argument_type.source_size != parameter_type.source_size ||
          emitted_count >= argument_capacity) {
        free(arguments);
        free(argument_abi_types);
        return unsupported_expr(context);
      }
      ir_val_t source = aggregate_value_address(context, argument);
      int temporary = allocate_scalar_temp(
          context, parameter_type.source_size, 8);
      if (context->status != IR_HIR_BUILD_OK ||
          source.type != IR_TY_PTR || temporary < 0) {
        free(arguments);
        free(argument_abi_types);
        return ir_val_none();
      }
      ir_inst_t *copy = ir_inst_new(IR_MEMCPY);
      if (!copy) {
        free(arguments);
        free(argument_abi_types);
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return ir_val_none();
      }
      copy->src1 = ir_val_vreg(temporary, IR_TY_PTR);
      copy->src2 = source;
      copy->alloca_size = parameter_type.source_size;
      if (!append_instruction(context, copy)) {
        free(arguments);
        free(argument_abi_types);
        return ir_val_none();
      }
      arguments[emitted_count] =
          ir_val_vreg(temporary, IR_TY_PTR);
      argument_abi_types[emitted_count++] = IR_TY_PTR;
      emitted_fixed_count = emitted_count;
      continue;
    }
    if (i >= signature.param_count && is_float_abi_type(argument_type) &&
        argument_type.type == IR_TY_F32) {
      parameter_type.type = IR_TY_F64;
      parameter_type.source_size = 8;
    }
    if (!argument || !is_direct_value_abi_type(parameter_type) ||
        (i < signature.param_count &&
         signature.params[i] != parameter_type.type)) {
      free(arguments);
      free(argument_abi_types);
      return unsupported_expr(context);
    }
    ir_val_t value = build_expr(context, argument);
    if (context->status == IR_HIR_BUILD_OK) {
      value = coerce_direct_value(
          context, value, argument_type,
          parameter_type);
    }
    if (context->status != IR_HIR_BUILD_OK) {
      free(arguments);
      free(argument_abi_types);
      return ir_val_none();
    }
    arguments[emitted_count] = value;
    argument_abi_types[emitted_count++] = parameter_type.type;
    if (i < signature.param_count || accepts_unprototyped_arguments)
      emitted_fixed_count = emitted_count;
  }

  int result_vreg = -1;
  if (!is_void_result && !is_complex_result) {
    result_vreg = new_vreg(context);
    if (result_vreg < 0) {
      free(arguments);
      free(argument_abi_types);
      return ir_val_none();
    }
  }
  ir_inst_t *call = ir_inst_new(IR_CALL);
  if (!call) {
    free(arguments);
    free(argument_abi_types);
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  if (is_complex_result) {
    int slot = allocate_scalar_temp(
        context, result_type.source_size,
        ir_type_size(result_type.type) >= 8 ? 8 : 4);
    if (slot < 0) {
      free(arguments);
      free(argument_abi_types);
      free(call);
      return ir_val_none();
    }
    call->dst = ir_val_vreg(slot, IR_TY_PTR);
    call->ret_complex_half =
        (unsigned char)ir_type_size(result_type.type);
  } else if (is_indirect_aggregate_result) {
    int result_area = allocate_scalar_temp(
        context, result_type.source_size, 8);
    if (result_area < 0) {
      free(arguments);
      free(argument_abi_types);
      free(call);
      return ir_val_none();
    }
    call->dst = ir_val_vreg(result_vreg, IR_TY_PTR);
    call->ret_struct_size = result_type.source_size;
    call->ret_struct_area =
        ir_val_vreg(result_area, IR_TY_PTR);
  } else if (!is_void_result) {
    call->dst = ir_val_vreg(result_vreg, result_type.type);
  }
  if (is_direct) {
    call->sym = (char *)name;
    call->sym_len = (int)name_length;
  } else {
    call->callee = callee_value;
  }
  call->args = arguments;
  call->arg_abi_types = argument_abi_types;
  if (emitted_count > INT_MAX) {
    free(arguments);
    free(argument_abi_types);
    free(call);
    return unsupported_expr(context);
  }
  call->nargs = (int)emitted_count;
  call->nargs_fixed = (short)emitted_fixed_count;
  call->is_variadic_call =
      signature.is_variadic && argument_count > signature.param_count;
  call->is_void_call = is_void_result ? 1 : 0;
  if (emitted_fixed_count > IR_CALLABLE_MAX_PARAMS) {
    free(arguments);
    free(argument_abi_types);
    free(call);
    return unsupported_expr(context);
  }
  ir_callable_sig_t expanded_signature = {
      .result = signature.result,
      .param_count = (unsigned char)emitted_fixed_count,
      .is_variadic = signature.is_variadic,
  };
  for (size_t i = 0; i < emitted_fixed_count; i++)
    expanded_signature.params[i] = argument_abi_types[i];
  set_callable_signature(call, &expanded_signature);
  if (!append_instruction(context, call)) return ir_val_none();
  if (is_void_result) return ir_val_none();
  if (is_complex_result) return call->dst;
  if (is_indirect_aggregate_result)
    return call->ret_struct_area;
  return ir_val_vreg(result_vreg, result_type.type);
}

static ir_val_t build_string_reference(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  size_t name_length = 0;
  const char *name = psx_hir_node_name(node, &name_length);
  int object_size = psx_hir_node_object_size(node);
  if (!name || name_length == 0 || object_size <= 0)
    return unsupported_expr(context);
  int result_vreg = new_vreg(context);
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
  if (!append_instruction(context, load)) return ir_val_none();
  return load->dst;
}

static ir_val_t build_function_reference(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  size_t name_length = 0;
  const char *name = psx_hir_node_name(node, &name_length);
  ir_abi_type_context_t abi = {
      .semantic_types = context->options->semantic_types,
      .record_layouts = context->options->record_layouts,
      .target = context->options->target,
  };
  ir_callable_sig_t signature;
  if (!name || name_length == 0 ||
      !ir_abi_callable_sig_from_type_id(
          &abi, psx_hir_node_qual_type(node).type_id, &signature))
    return unsupported_expr(context);
  int result_vreg = new_vreg(context);
  if (result_vreg < 0) return ir_val_none();
  ir_inst_t *load = ir_inst_new(IR_LOAD_SYM);
  if (!load) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  load->dst = ir_val_vreg(result_vreg, IR_TY_PTR);
  load->sym = (char *)name;
  load->sym_len = (int)name_length;
  load->is_got_funcref = 1;
  load->is_function_symbol = 1;
  set_callable_signature(load, &signature);
  if (!append_instruction(context, load)) return ir_val_none();
  return load->dst;
}

static ir_val_t build_expr(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  if (!node || context->status != IR_HIR_BUILD_OK)
    return ir_val_none();
  if (psx_hir_node_kind(node) == PSX_HIR_VLA_ALLOC)
    return build_vla_allocation(context, node);
  if (psx_hir_node_kind(node) == PSX_HIR_STMT_EXPR) {
    const psx_hir_node_t *prefix = child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    const psx_hir_node_t *value = child_for_edge(
        context, node, PSX_HIR_EDGE_RHS, 0);
    if (!prefix || psx_hir_node_kind(prefix) != PSX_HIR_BLOCK ||
        !value || !build_statement(context, prefix))
      return unsupported_expr(context);
    return build_expr(context, value);
  }
  ir_abi_param_info_t type = classify_node_type(context, node);
  int is_void = node_type_kind(context, node) == PSX_TYPE_VOID;
  if (psx_hir_node_kind(node) == PSX_HIR_CALL && is_void)
    return build_scalar_or_void_call(context, node, type);
  if (psx_hir_node_kind(node) == PSX_HIR_CAST && is_void) {
    const psx_hir_node_t *operand = child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    if (!operand) return unsupported_expr(context);
    (void)build_expr(context, operand);
    return ir_val_none();
  }
  if (psx_hir_node_kind(node) == PSX_HIR_TERNARY && is_void)
    return build_void_ternary(context, node);
  if (is_complex_abi_type(type)) {
    psx_hir_node_kind_t kind = psx_hir_node_kind(node);
    if (kind == PSX_HIR_LOCAL || kind == PSX_HIR_GLOBAL ||
        kind == PSX_HIR_DEREF)
      return lvalue_address(context, node);
    if (kind == PSX_HIR_CALL)
      return build_scalar_or_void_call(context, node, type);
    if (kind == PSX_HIR_CAST) {
      const psx_hir_node_t *operand = child_for_edge(
          context, node, PSX_HIR_EDGE_LHS, 0);
      if (!operand) return unsupported_expr(context);
      return materialize_complex_operand(context, operand, type);
    }
    if (kind == PSX_HIR_ASSIGN)
      return build_complex_assignment(context, node, type);
    if (kind == PSX_HIR_NEGATE)
      return build_complex_negate(context, node, type);
    if (kind == PSX_HIR_ADD || kind == PSX_HIR_SUB ||
        kind == PSX_HIR_MUL || kind == PSX_HIR_DIV)
      return build_complex_binary(context, node, type);
    if (kind == PSX_HIR_COMMA) {
      const psx_hir_node_t *left = child_for_edge(
          context, node, PSX_HIR_EDGE_LHS, 0);
      const psx_hir_node_t *right = child_for_edge(
          context, node, PSX_HIR_EDGE_RHS, 0);
      if (!left || !right) return unsupported_expr(context);
      (void)build_expr(context, left);
      if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
      return build_expr(context, right);
    }
    return unsupported_expr(context);
  }
  if (is_indirect_aggregate_abi_type(type)) {
    psx_hir_node_kind_t kind = psx_hir_node_kind(node);
    if (kind == PSX_HIR_LOCAL || kind == PSX_HIR_GLOBAL ||
        kind == PSX_HIR_DEREF || kind == PSX_HIR_CALL ||
        kind == PSX_HIR_ASSIGN || kind == PSX_HIR_COMMA ||
        kind == PSX_HIR_TERNARY)
      return aggregate_value_address(context, node);
    return unsupported_expr(context);
  }
  if (!is_direct_value_abi_type(type)) {
    return unsupported_expr(context);
  }
  if (psx_hir_node_kind(node) == PSX_HIR_CREAL ||
      psx_hir_node_kind(node) == PSX_HIR_CIMAG)
    return build_complex_component(context, node, type);
  if (psx_hir_node_kind(node) == PSX_HIR_NUMBER) {
    if (is_float_abi_type(type)) {
      int result = new_vreg(context);
      if (result < 0) return ir_val_none();
      ir_inst_t *load = ir_inst_new(IR_LOAD_FP_IMM);
      if (!load) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return ir_val_none();
      }
      load->dst = ir_val_vreg(result, type.type);
      load->src1 = ir_val_fp_imm(
          type.type, psx_hir_node_floating_value(node));
      if (!append_instruction(context, load)) return ir_val_none();
      return load->dst;
    }
    if (type.param_class != IR_ABI_PARAM_INTEGER)
      return unsupported_expr(context);
    return ir_val_imm(type.type, psx_hir_node_integer_value(node));
  }
  if (psx_hir_node_kind(node) == PSX_HIR_STRING)
    return build_string_reference(context, node);
  if (psx_hir_node_kind(node) == PSX_HIR_FUNCTION_REF)
    return build_function_reference(context, node);
  if (psx_hir_node_kind(node) == PSX_HIR_VA_ARG_AREA) {
    if (type.param_class != IR_ABI_PARAM_POINTER)
      return unsupported_expr(context);
    int result = new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *area = ir_inst_new(IR_VA_ARG_AREA);
    if (!area) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    area->dst = ir_val_vreg(result, IR_TY_PTR);
    if (!append_instruction(context, area)) return ir_val_none();
    return area->dst;
  }
  if (psx_hir_node_kind(node) == PSX_HIR_LOGAND)
    return build_short_circuit(context, node, 1);
  if (psx_hir_node_kind(node) == PSX_HIR_LOGOR)
    return build_short_circuit(context, node, 0);
  if (psx_hir_node_kind(node) == PSX_HIR_TERNARY)
    return build_scalar_ternary(context, node, type);
  if (psx_hir_node_kind(node) == PSX_HIR_PRE_INC ||
      psx_hir_node_kind(node) == PSX_HIR_PRE_DEC ||
      psx_hir_node_kind(node) == PSX_HIR_POST_INC ||
      psx_hir_node_kind(node) == PSX_HIR_POST_DEC)
    return build_inc_dec(context, node, type);

  if (psx_hir_node_kind(node) == PSX_HIR_GLOBAL) {
    ir_val_t pointer = global_address(context, node);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    psx_type_kind_t global_kind = node_type_kind(context, node);
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
          scalar_storage_type(type));
    int result = new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(result, scalar_storage_type(type));
    load->src1 = pointer;
    load->is_unsigned = type.is_unsigned ? 1 : 0;
    if (!append_instruction(context, load)) return ir_val_none();
    return load->dst;
  }

  if (psx_hir_node_kind(node) == PSX_HIR_ADDRESS) {
    const psx_hir_node_t *operand = child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    if (type.param_class != IR_ABI_PARAM_POINTER || !operand)
      return unsupported_expr(context);
    return lvalue_address(context, operand);
  }

  if (psx_hir_node_kind(node) == PSX_HIR_DEREF) {
    ir_val_t pointer = lvalue_address(context, node);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    psx_type_kind_t dereferenced_kind = node_type_kind(context, node);
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
          scalar_storage_type(type));
    int result = new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(result, scalar_storage_type(type));
    load->src1 = pointer;
    load->is_unsigned = type.is_unsigned ? 1 : 0;
    if (!append_instruction(context, load)) return ir_val_none();
    return load->dst;
  }

  if (psx_hir_node_kind(node) == PSX_HIR_LOCAL) {
    int pointer = local_address(context, node);
    if (pointer < 0) return ir_val_none();
    psx_type_kind_t local_kind = node_type_kind(context, node);
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
          bit_offset, bit_is_signed, scalar_storage_type(type));
    int result = new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *load = ir_inst_new(IR_LOAD);
    if (!load) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    load->dst = ir_val_vreg(result, scalar_storage_type(type));
    load->src1 = ir_val_vreg(pointer, IR_TY_PTR);
    load->is_unsigned = type.is_unsigned ? 1 : 0;
    if (!append_instruction(context, load)) return ir_val_none();
    return load->dst;
  }

  if (psx_hir_node_kind(node) == PSX_HIR_CAST ||
      psx_hir_node_kind(node) == PSX_HIR_FP_TO_INT ||
      psx_hir_node_kind(node) == PSX_HIR_INT_TO_FP) {
    const psx_hir_node_t *operand = child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    if (!operand) return unsupported_expr(context);
    ir_val_t value = build_expr(context, operand);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    return coerce_direct_value(
        context, value, classify_node_type(context, operand), type);
  }

  if (psx_hir_node_kind(node) == PSX_HIR_NEGATE)
    return build_scalar_negate(context, node, type);

  if (psx_hir_node_kind(node) == PSX_HIR_COMMA) {
    const psx_hir_node_t *left = child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    const psx_hir_node_t *right = child_for_edge(
        context, node, PSX_HIR_EDGE_RHS, 0);
    if (!left || !right) return unsupported_expr(context);
    (void)build_expr(context, left);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    return build_expr(context, right);
  }

  if (psx_hir_node_kind(node) == PSX_HIR_CALL)
    return build_scalar_or_void_call(context, node, type);

  if (psx_hir_node_kind(node) == PSX_HIR_ASSIGN) {
    const psx_hir_node_t *target = child_for_edge(
        context, node, PSX_HIR_EDGE_LHS, 0);
    const psx_hir_node_t *value_node = child_for_edge(
        context, node, PSX_HIR_EDGE_RHS, 0);
    if (!target || !value_node ||
        (psx_hir_node_kind(target) != PSX_HIR_LOCAL &&
         psx_hir_node_kind(target) != PSX_HIR_DEREF &&
         psx_hir_node_kind(target) != PSX_HIR_GLOBAL))
      return unsupported_expr(context);
    ir_val_t value = build_expr(context, value_node);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    ir_abi_param_info_t target_type = classify_node_type(context, target);
    value = coerce_direct_value(
        context, value, classify_node_type(context, value_node), target_type);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    ir_val_t pointer = lvalue_address(context, target);
    if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    int bit_width = 0;
    int bit_offset = 0;
    int bit_is_signed = 0;
    if (psx_hir_node_bitfield_info(
            target, &bit_width, &bit_offset, &bit_is_signed)) {
      (void)bit_is_signed;
      return emit_bitfield_store(
          context, pointer, value, bit_width, bit_offset);
    }
    ir_inst_t *store = ir_inst_new(IR_STORE);
    if (!store) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    store->src1 = pointer;
    store->src2 = value;
    if (!append_instruction(context, store)) return ir_val_none();
    return value;
  }

  const psx_hir_node_t *lhs = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *rhs = child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  if (!lhs || !rhs) return unsupported_expr(context);
  ir_val_t left = build_expr(context, lhs);
  ir_val_t right = build_expr(context, rhs);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();

  ir_abi_param_info_t left_type = classify_node_type(context, lhs);
  ir_abi_param_info_t right_type = classify_node_type(context, rhs);
  if (is_complex_abi_type(left_type) || is_complex_abi_type(right_type)) {
    if (!is_complex_abi_type(left_type) ||
        !is_complex_abi_type(right_type) ||
        left_type.type != right_type.type ||
        left_type.source_size != right_type.source_size)
      return unsupported_expr(context);
    return build_complex_comparison(
        context, psx_hir_node_kind(node), left, right, left_type);
  }
  int is_float = is_float_abi_type(left_type) ||
                 is_float_abi_type(right_type);
  if (is_float) {
    psx_hir_node_kind_t kind = psx_hir_node_kind(node);
    if (kind != PSX_HIR_ADD && kind != PSX_HIR_SUB &&
        kind != PSX_HIR_MUL && kind != PSX_HIR_DIV &&
        kind != PSX_HIR_EQ && kind != PSX_HIR_NE &&
        kind != PSX_HIR_LT && kind != PSX_HIR_LE)
      return unsupported_expr(context);
    ir_type_t fp_type = left_type.type == IR_TY_F64 ||
                                right_type.type == IR_TY_F64
                            ? IR_TY_F64
                            : IR_TY_F32;
    ir_abi_param_info_t arithmetic_type = {
        .type = fp_type,
        .param_class = IR_ABI_PARAM_FLOAT,
        .source_size = ir_type_size(fp_type),
    };
    left = coerce_direct_value(
        context, left, left_type, arithmetic_type);
    if (context->status == IR_HIR_BUILD_OK)
      right = coerce_direct_value(
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
      default: return unsupported_expr(context);
    }
    int result_vreg = new_vreg(context);
    if (result_vreg < 0) return ir_val_none();
    ir_inst_t *instruction = ir_inst_new(fp_op);
    if (!instruction) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    instruction->dst = ir_val_vreg(result_vreg, type.type);
    instruction->src1 = left;
    instruction->src2 = right;
    if (!append_instruction(context, instruction)) return ir_val_none();
    return instruction->dst;
  }
  const psx_type_t *left_semantic_type = psx_semantic_type_table_lookup(
      context->options->semantic_types,
      psx_hir_node_qual_type(lhs).type_id);
  const psx_type_t *right_semantic_type = psx_semantic_type_table_lookup(
      context->options->semantic_types,
      psx_hir_node_qual_type(rhs).type_id);
  int uac_is_unsigned =
      ps_type_usual_arithmetic_result_is_unsigned_for_target(
          left_semantic_type, right_semantic_type,
          context->options->target);
  int shift_is_unsigned =
      ps_type_integer_promotion_is_unsigned_for_target(
          left_semantic_type, context->options->target);
  ir_op_t op;
  switch (psx_hir_node_kind(node)) {
    case PSX_HIR_ADD: op = IR_ADD; break;
    case PSX_HIR_SUB: op = IR_SUB; break;
    case PSX_HIR_MUL: op = IR_MUL; break;
    case PSX_HIR_DIV:
      if (left_type.param_class != IR_ABI_PARAM_INTEGER)
        return unsupported_expr(context);
      op = uac_is_unsigned ? IR_UDIV : IR_DIV;
      break;
    case PSX_HIR_MOD:
      if (left_type.param_class != IR_ABI_PARAM_INTEGER)
        return unsupported_expr(context);
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
      op = left_type.param_class == IR_ABI_PARAM_POINTER ||
                   uac_is_unsigned ? IR_ULT : IR_LT;
      break;
    case PSX_HIR_LE:
      op = left_type.param_class == IR_ABI_PARAM_POINTER ||
                   uac_is_unsigned ? IR_ULE : IR_LE;
      break;
    default: return unsupported_expr(context);
  }
  int result_vreg = new_vreg(context);
  if (result_vreg < 0) return ir_val_none();
  ir_inst_t *instruction = ir_inst_new(op);
  if (!instruction) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  instruction->dst = ir_val_vreg(result_vreg, type.type);
  instruction->src1 = left;
  instruction->src2 = right;
  if (!append_instruction(context, instruction)) return ir_val_none();
  return instruction->dst;
}

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
    ir_block_t *block = new_block(context);
    if (!block) return 0;
    target->cases[target->case_count++] =
        (hir_case_target_t){node, block};
  } else if (psx_hir_node_kind(node) == PSX_HIR_DEFAULT) {
    if (target->default_node) {
      context->status = IR_HIR_BUILD_INVALID;
      return 0;
    }
    target->default_node = node;
    target->default_block = new_block(context);
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
  const psx_hir_node_t *control = child_for_edge(
      context, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *body = child_for_edge(
      context, node, PSX_HIR_EDGE_RHS, 0);
  if (!control || !body || context->switch_depth >=
      sizeof(context->switch_targets) /
          sizeof(context->switch_targets[0])) {
    context->status = IR_HIR_BUILD_UNSUPPORTED;
    return 0;
  }
  ir_val_t control_value = build_expr(context, control);
  if (context->status != IR_HIR_BUILD_OK ||
      !is_integer_ir_type(control_value.type))
    return 0;

  hir_switch_target_t *target =
      &context->switch_targets[context->switch_depth++];
  memset(target, 0, sizeof(*target));
  target->end_block = new_block(context);
  if (!target->end_block ||
      !collect_switch_targets(context, body, target)) {
    context->switch_depth--;
    return 0;
  }

  for (size_t i = 0; i < target->case_count; i++) {
    int compare_vreg = new_vreg(context);
    ir_block_t *next_block = new_block(context);
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
    if (!append_instruction(context, compare) ||
        !emit_conditional_branch(
            context, compare->dst, target->cases[i].block, next_block) ||
        !switch_to_block(context, next_block)) {
      context->switch_depth--;
      return 0;
    }
  }
  ir_block_t *fallback = target->default_block
                             ? target->default_block : target->end_block;
  ir_block_t *body_entry = new_block(context);
  ir_block_t *outer_continue = context->loop_depth
                                   ? context->loop_targets[
                                         context->loop_depth - 1]
                                         .continue_block
                                   : NULL;
  if (!body_entry || !emit_branch(context, fallback) ||
      !switch_to_block(context, body_entry) ||
      !push_loop(context, outer_continue, target->end_block)) {
    context->switch_depth--;
    return 0;
  }
  int built = build_statement(context, body);
  pop_loop(context);
  if (!built || !emit_branch(context, target->end_block) ||
      !switch_to_block(context, target->end_block)) {
    context->switch_depth--;
    return 0;
  }
  context->switch_depth--;
  return 1;
}

static int build_statement(
    hir_ir_context_t *context, const psx_hir_node_t *node) {
  if (!node || context->status != IR_HIR_BUILD_OK) return 0;
  if (psx_hir_node_role(node) == PSX_HIR_ROLE_EXPRESSION) {
    (void)build_expr(context, node);
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
        if (current_block_is_terminated(context)) {
          psx_hir_node_kind_t child_kind = psx_hir_node_kind(child);
          if (child_kind != PSX_HIR_CASE &&
              child_kind != PSX_HIR_DEFAULT &&
              child_kind != PSX_HIR_LABEL) {
            ir_block_t *dead_block = new_block(context);
            if (!dead_block || !switch_to_block(context, dead_block))
              return 0;
          }
        }
        if (!build_statement(context, child)) return 0;
      }
      return 1;
    case PSX_HIR_RETURN: {
      const psx_hir_node_t *value = child_for_edge(
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
        return append_instruction(context, ret);
      }
      if (is_complex_abi_type(context->return_info)) {
        ir_abi_param_info_t value_type = classify_node_type(
            context, value);
        ir_val_t result = materialize_complex_operand(
            context, value, context->return_info);
        if (context->status != IR_HIR_BUILD_OK ||
            !is_complex_abi_type(value_type) ||
            result.type != IR_TY_PTR)
          return 0;
        ir_inst_t *ret = ir_inst_new(IR_RET);
        if (!ret) {
          context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
          return 0;
        }
        ret->src1 = result;
        ret->ret_complex_half =
            (unsigned char)ir_type_size(context->return_info.type);
        return append_instruction(context, ret);
      }
      if (is_indirect_aggregate_abi_type(context->return_info)) {
        ir_abi_param_info_t value_type = classify_node_type(
            context, value);
        ir_val_t source = aggregate_value_address(context, value);
        if (context->status != IR_HIR_BUILD_OK ||
            value_type.param_class != IR_ABI_PARAM_AGGREGATE ||
            value_type.source_size != context->return_info.source_size ||
            source.type != IR_TY_PTR ||
            context->function->ret_area_vreg < 0)
          return 0;
        ir_inst_t *copy = ir_inst_new(IR_MEMCPY);
        if (!copy) {
          context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
          return 0;
        }
        copy->src1 = ir_val_vreg(
            context->function->ret_area_vreg, IR_TY_PTR);
        copy->src2 = source;
        copy->alloca_size = context->return_info.source_size;
        if (!append_instruction(context, copy)) return 0;
        ir_inst_t *ret = ir_inst_new(IR_RET);
        if (!ret) {
          context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
          return 0;
        }
        ret->src1 = ir_val_none();
        return append_instruction(context, ret);
      }
      if (context->returns_void) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      ir_val_t result = build_expr(context, value);
      if (context->status != IR_HIR_BUILD_OK) return 0;
      ir_abi_param_info_t value_type = classify_node_type(context, value);
      result = coerce_direct_value(
          context, result, value_type, context->return_info);
      if (context->status != IR_HIR_BUILD_OK) return 0;
      ir_inst_t *ret = ir_inst_new(IR_RET);
      if (!ret) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return 0;
      }
      ret->src1 = result;
      if (!append_instruction(context, ret)) return 0;
      return 1;
    }
    case PSX_HIR_VLA_ALLOC:
      (void)build_vla_allocation(context, node);
      return context->status == IR_HIR_BUILD_OK;
    case PSX_HIR_IF: {
      const psx_hir_node_t *condition = child_for_edge(
          context, node, PSX_HIR_EDGE_LHS, 0);
      const psx_hir_node_t *then_statement = child_for_edge(
          context, node, PSX_HIR_EDGE_RHS, 0);
      const psx_hir_node_t *else_statement = child_for_edge(
          context, node, PSX_HIR_EDGE_ELSE, 0);
      if (!condition || !then_statement) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      ir_val_t value = build_expr(context, condition);
      if (context->status != IR_HIR_BUILD_OK) return 0;
      ir_block_t *then_block = new_block(context);
      ir_block_t *merge_block = new_block(context);
      ir_block_t *else_block = else_statement
                                   ? new_block(context) : merge_block;
      if (!then_block || !merge_block || !else_block ||
          !emit_conditional_branch(
              context, value, then_block, else_block) ||
          !switch_to_block(context, then_block) ||
          !build_statement(context, then_statement) ||
          !emit_branch(context, merge_block)) return 0;
      if (else_statement &&
          (!switch_to_block(context, else_block) ||
           !build_statement(context, else_statement) ||
           !emit_branch(context, merge_block))) return 0;
      return switch_to_block(context, merge_block);
    }
    case PSX_HIR_WHILE:
    case PSX_HIR_DO_WHILE: {
      const psx_hir_node_t *condition = child_for_edge(
          context, node, PSX_HIR_EDGE_LHS, 0);
      const psx_hir_node_t *body = child_for_edge(
          context, node, PSX_HIR_EDGE_RHS, 0);
      if (!condition || !body) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      ir_block_t *condition_block = new_block(context);
      ir_block_t *body_block = new_block(context);
      ir_block_t *exit_block = new_block(context);
      if (!condition_block || !body_block || !exit_block) return 0;
      if (psx_hir_node_kind(node) == PSX_HIR_WHILE) {
        if (!emit_branch(context, condition_block) ||
            !switch_to_block(context, condition_block)) return 0;
        ir_val_t value = build_expr(context, condition);
        if (context->status != IR_HIR_BUILD_OK ||
            !emit_conditional_branch(
                context, value, body_block, exit_block)) return 0;
      } else if (!emit_branch(context, body_block)) {
        return 0;
      }
      if (!push_loop(context, condition_block, exit_block) ||
          !switch_to_block(context, body_block) ||
          !build_statement(context, body)) return 0;
      pop_loop(context);
      if (!emit_branch(context, condition_block)) return 0;
      if (psx_hir_node_kind(node) == PSX_HIR_DO_WHILE) {
        if (!switch_to_block(context, condition_block)) return 0;
        ir_val_t value = build_expr(context, condition);
        if (context->status != IR_HIR_BUILD_OK ||
            !emit_conditional_branch(
                context, value, body_block, exit_block)) return 0;
      }
      return switch_to_block(context, exit_block);
    }
    case PSX_HIR_FOR: {
      const psx_hir_node_t *initial = child_for_edge(
          context, node, PSX_HIR_EDGE_INIT, 0);
      const psx_hir_node_t *condition = child_for_edge(
          context, node, PSX_HIR_EDGE_LHS, 0);
      const psx_hir_node_t *increment = child_for_edge(
          context, node, PSX_HIR_EDGE_INCREMENT, 0);
      const psx_hir_node_t *body = child_for_edge(
          context, node, PSX_HIR_EDGE_RHS, 0);
      if (!body) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      if (initial && !build_statement(context, initial)) return 0;
      ir_block_t *condition_block = new_block(context);
      ir_block_t *body_block = new_block(context);
      ir_block_t *increment_block = new_block(context);
      ir_block_t *exit_block = new_block(context);
      if (!condition_block || !body_block || !increment_block ||
          !exit_block || !emit_branch(context, condition_block) ||
          !switch_to_block(context, condition_block)) return 0;
      if (condition) {
        ir_val_t value = build_expr(context, condition);
        if (context->status != IR_HIR_BUILD_OK ||
            !emit_conditional_branch(
                context, value, body_block, exit_block)) return 0;
      } else if (!emit_branch(context, body_block)) {
        return 0;
      }
      if (!push_loop(context, increment_block, exit_block) ||
          !switch_to_block(context, body_block) ||
          !build_statement(context, body)) return 0;
      pop_loop(context);
      if (!emit_branch(context, increment_block) ||
          !switch_to_block(context, increment_block)) return 0;
      if (increment && !build_statement(context, increment)) return 0;
      if (!emit_branch(context, condition_block)) return 0;
      return switch_to_block(context, exit_block);
    }
    case PSX_HIR_SWITCH:
      return build_switch_statement(context, node);
    case PSX_HIR_CASE:
    case PSX_HIR_DEFAULT: {
      ir_block_t *target = current_case_block(context, node);
      const psx_hir_node_t *statement = child_for_edge(
          context, node, PSX_HIR_EDGE_RHS, 0);
      if (!target) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      if (!emit_branch(context, target) ||
          !switch_to_block(context, target)) return 0;
      return !statement || build_statement(context, statement);
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
      ir_block_t *dead_block = new_block(context);
      return dead_block && emit_branch(context, destination) &&
             switch_to_block(context, dead_block);
    }
    case PSX_HIR_GOTO: {
      size_t name_length = 0;
      const char *name = psx_hir_node_name(node, &name_length);
      ir_block_t *target = lookup_label_block(
          context, name, name_length);
      ir_block_t *dead_block = new_block(context);
      if (!target || !dead_block) {
        if (context->status == IR_HIR_BUILD_OK)
          context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      return emit_branch(context, target) &&
             switch_to_block(context, dead_block);
    }
    case PSX_HIR_LABEL: {
      size_t name_length = 0;
      const char *name = psx_hir_node_name(node, &name_length);
      ir_block_t *target = lookup_label_block(
          context, name, name_length);
      const psx_hir_node_t *statement = child_for_edge(
          context, node, PSX_HIR_EDGE_RHS, 0);
      if (!target) {
        context->status = IR_HIR_BUILD_INVALID;
        return 0;
      }
      if (!emit_branch(context, target) ||
          !switch_to_block(context, target)) return 0;
      return !statement || build_statement(context, statement);
    }
    default:
      context->status = IR_HIR_BUILD_UNSUPPORTED;
      return 0;
  }
}

ir_module_t *ir_build_function_module_from_hir(
    const psx_hir_module_t *hir, psx_hir_node_id_t function_root,
    const ir_build_options_t *options, ir_hir_build_status_t *status) {
  if (status) *status = IR_HIR_BUILD_INVALID;
  if (!hir || !options || !options->target || !options->semantic_types ||
      !options->record_layouts) {
    return NULL;
  }
  if (options->continuation) {
    if (status) *status = IR_HIR_BUILD_UNSUPPORTED;
    return NULL;
  }
  const psx_hir_node_t *root = psx_hir_module_lookup(hir, function_root);
  if (!root || psx_hir_node_kind(root) != PSX_HIR_FUNCTION) return NULL;

  ir_abi_type_context_t abi = {
      .semantic_types = options->semantic_types,
      .record_layouts = options->record_layouts,
      .target = options->target,
  };
  ir_callable_sig_t signature;
  psx_type_id_t signature_id =
      psx_hir_node_attached_qual_type(root).type_id;
  psx_type_id_t result_type_id = psx_semantic_type_table_base(
      options->semantic_types, signature_id).type_id;
  ir_abi_param_info_t return_info = ir_abi_classify_type_id(
      &abi, result_type_id);
  const psx_type_t *result_type = psx_semantic_type_table_lookup(
      options->semantic_types, result_type_id);
  int returns_void = result_type && result_type->kind == PSX_TYPE_VOID;
  if (!ir_abi_callable_sig_from_type_id(&abi, signature_id, &signature) ||
      (!returns_void && !is_complex_abi_type(return_info) &&
       !is_indirect_aggregate_abi_type(return_info) &&
       !is_direct_value_abi_type(return_info)) ||
      signature.result != (returns_void ? IR_TY_VOID : return_info.type)) {
    if (status) *status = IR_HIR_BUILD_UNSUPPORTED;
    return NULL;
  }
  size_t name_length = 0;
  const char *name = psx_hir_node_name(root, &name_length);
  const psx_hir_node_t *body = child_for_edge(
      &(hir_ir_context_t){.hir = hir}, root,
      PSX_HIR_EDGE_FUNCTION_BODY, 0);
  if (!name || name_length == 0 || !body) return NULL;

  hir_ir_context_t context = {
      .hir = hir,
      .options = options,
      .status = IR_HIR_BUILD_OK,
      .return_info = return_info,
      .returns_void = returns_void,
  };
  context.module = ir_module_new();
  if (!context.module) {
    if (status) *status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return NULL;
  }
  context.function = ir_func_new(
      context.module, name, (int)name_length, signature.result);
  if (!context.function) {
    ir_module_free(context.module);
    if (status) *status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return NULL;
  }
  context.function->is_static = psx_hir_node_is_static_function(root);
  if (is_complex_abi_type(return_info))
    context.function->ret_complex_half =
        ir_type_size(return_info.type);
  if (is_indirect_aggregate_abi_type(return_info)) {
    context.function->ret_struct_size = return_info.source_size;
    int return_area = new_vreg(&context);
    if (return_area < 0) {
      ir_module_free(context.module);
      if (status) *status = context.status;
      return NULL;
    }
    ir_inst_t *parameter = ir_inst_new(IR_PARAM);
    if (!parameter) {
      ir_module_free(context.module);
      if (status) *status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return NULL;
    }
    parameter->dst = ir_val_vreg(return_area, IR_TY_PTR);
    parameter->src1 = ir_val_imm(IR_TY_I32, -1);
    if (!append_instruction(&context, parameter)) {
      ir_module_free(context.module);
      if (status) *status = context.status;
      return NULL;
    }
    context.function->ret_area_vreg = return_area;
  }
  context.function->is_variadic = signature.is_variadic;
  context.function->nargs_fixed = signature.param_count;
  const psx_type_t *function_type = psx_semantic_type_table_lookup(
      options->semantic_types, signature_id);
  int signature_length = ps_type_format_canonical_signature_for_target(
      function_type, options->target, NULL, 0);
  if (signature_length < 0) {
    ir_module_free(context.module);
    if (status) *status = IR_HIR_BUILD_INVALID;
    return NULL;
  }
  context.function->c_signature = malloc((size_t)signature_length + 1);
  if (!context.function->c_signature ||
      ps_type_format_canonical_signature_for_target(
          function_type, options->target,
          context.function->c_signature,
          (size_t)signature_length + 1) != signature_length) {
    ir_module_free(context.module);
    if (status) *status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return NULL;
  }
  context.function->c_signature_len = signature_length;
  if (!setup_scalar_parameters(&context, root, &signature) ||
      !emit_vla_parameter_strides(&context, root) ||
      !preallocate_local_storage(&context, body) ||
      !collect_label_blocks(&context, body)) {
    ir_module_free(context.module);
    if (status) *status = context.status;
    return NULL;
  }
  if (!build_statement(&context, body)) {
    ir_module_free(context.module);
    if (status) *status = context.status;
    return NULL;
  }
  if (!append_implicit_return(&context, name, name_length)) {
    ir_module_free(context.module);
    if (status) *status = context.status;
    return NULL;
  }
  if (!context.function->cur_block || !context.function->cur_block->tail ||
      (!current_block_is_terminated(&context) &&
       (context.function->cur_block == context.function->entry ||
        block_has_predecessor(
            context.function, context.function->cur_block)))) {
    ir_module_free(context.module);
    if (status) *status = IR_HIR_BUILD_UNSUPPORTED;
    return NULL;
  }
  if (status) *status = IR_HIR_BUILD_OK;
  return context.module;
}
