#include "hir_ir_builder.h"
#include "hir_ir_builder_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mir_type_lowering.h"
#include "function_type_lowering.h"
#include "../diag/diag.h"
#include "../parser/type.h"
#include "../semantic/type_identity.h"
#include "../type_layout.h"

const psx_hir_node_t *hir_ir_child_for_edge(
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

size_t hir_ir_child_count_for_edge(
    const psx_hir_node_t *node, psx_hir_edge_kind_t edge) {
  size_t count = 0;
  for (size_t i = 0; i < psx_hir_node_child_count(node); i++) {
    if (psx_hir_node_child_edge_at(node, i) == edge) count++;
  }
  return count;
}

ir_mir_type_info_t hir_ir_classify_node_type(
    const hir_ir_context_t *context, const psx_hir_node_t *node) {
  ir_mir_type_context_t type_context = {
      .semantic_types = context->options->semantic_types,
      .record_layouts = context->options->record_layouts,
      .target = context->options->target,
  };
  return ir_mir_classify_type_id(
      &type_context, psx_hir_node_qual_type(node).type_id);
}

psx_type_kind_t hir_ir_node_type_kind(
    const hir_ir_context_t *context, const psx_hir_node_t *node) {
  const psx_type_t *type =
      node ? psx_semantic_type_table_lookup(
                 context->options->semantic_types,
                 psx_hir_node_qual_type(node).type_id)
           : NULL;
  return type ? type->kind : PSX_TYPE_INVALID;
}

const psx_type_t *hir_ir_node_semantic_type(
    const hir_ir_context_t *context, const psx_hir_node_t *node) {
  return node ? psx_semantic_type_table_lookup(
                    context->options->semantic_types,
                    psx_hir_node_qual_type(node).type_id)
              : NULL;
}

ir_val_t hir_ir_unsupported_expr(hir_ir_context_t *context) {
  context->status = IR_HIR_BUILD_UNSUPPORTED;
  return ir_val_none();
}

int hir_ir_new_vreg(hir_ir_context_t *context) {
  int vreg = ir_func_new_vreg(context->function);
  if (vreg < 0) context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
  return vreg;
}

int hir_ir_append_instruction(
    hir_ir_context_t *context, ir_inst_t *instruction) {
  if (!instruction) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  ir_func_append_inst(context->function, instruction);
  return 1;
}

int hir_ir_is_integer_type(ir_type_t type);
int hir_ir_is_float_type(ir_type_t type);
int hir_ir_is_direct_value_type(ir_mir_type_info_t type);
ir_val_t hir_ir_scalar_truth_value(
    hir_ir_context_t *context, ir_val_t value);
ir_val_t hir_ir_pointer_with_offset(
    hir_ir_context_t *context, ir_val_t base, int offset);
int hir_ir_store_direct_value(
    hir_ir_context_t *context, ir_val_t pointer, ir_val_t value);
ir_val_t hir_ir_materialize_complex_operand(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t target_type);
ir_val_t hir_ir_aggregate_value_address(
    hir_ir_context_t *context, const psx_hir_node_t *node);

static int append_implicit_return(
    hir_ir_context_t *context, const char *name, size_t name_length) {
  if (hir_ir_cfg_current_block_terminated(context)) return 1;
  if (context->function->cur_block != context->function->entry &&
      !hir_ir_cfg_block_has_predecessor(
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
  } else if (hir_ir_is_direct_value_type(context->return_info)) {
    ret->src1 = hir_ir_is_float_type(context->return_info.type)
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
  return hir_ir_append_instruction(context, ret);
}

int hir_ir_emit_conditional_branch(
    hir_ir_context_t *context, ir_val_t condition,
    ir_block_t *if_true, ir_block_t *if_false) {
  if (hir_ir_cfg_current_block_terminated(context)) return 1;
  if (hir_ir_is_float_type(condition.type)) {
    condition = hir_ir_scalar_truth_value(context, condition);
    if (context->status != IR_HIR_BUILD_OK) return 0;
  }
  if (!hir_ir_is_integer_type(condition.type) && condition.type != IR_TY_PTR) {
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
  return hir_ir_append_instruction(context, branch);
}

int hir_ir_is_integer_type(ir_type_t type) {
  return type == IR_TY_I8 || type == IR_TY_I16 ||
         type == IR_TY_I32 || type == IR_TY_I64;
}

int hir_ir_is_float_type(ir_type_t type) {
  return type == IR_TY_F32 || type == IR_TY_F64;
}

static int is_scalar_mir_type(ir_mir_type_info_t type) {
  return type.type_class == IR_MIR_TYPE_INTEGER ||
         type.type_class == IR_MIR_TYPE_POINTER;
}

int hir_ir_is_float_value_type(ir_mir_type_info_t type) {
  return type.type_class == IR_MIR_TYPE_FLOAT &&
         hir_ir_is_float_type(type.type) &&
         type.source_size == ir_type_size(type.type);
}

int hir_ir_is_complex_type(ir_mir_type_info_t type) {
  return type.type_class == IR_MIR_TYPE_COMPLEX &&
         hir_ir_is_float_type(type.type) &&
         type.source_size == 2 * ir_type_size(type.type);
}

int hir_ir_is_scalar_value_type(ir_mir_type_info_t type) {
  return is_scalar_mir_type(type) || hir_ir_is_float_value_type(type);
}

int hir_ir_is_direct_value_type(ir_mir_type_info_t type) {
  return hir_ir_is_scalar_value_type(type);
}

ir_type_t hir_ir_integer_storage_type(ir_mir_type_info_t type) {
  if (type.source_size >= 8) return IR_TY_I64;
  if (type.source_size == 4) return IR_TY_I32;
  if (type.source_size == 2) return IR_TY_I16;
  return IR_TY_I8;
}

ir_type_t hir_ir_scalar_storage_type(ir_mir_type_info_t type) {
  if (hir_ir_is_float_value_type(type)) return type.type;
  return type.type_class == IR_MIR_TYPE_POINTER
             ? IR_TY_PTR : hir_ir_integer_storage_type(type);
}

long long hir_ir_normalize_integer_immediate(
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

int hir_ir_local_storage_address(
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
  int pointer = hir_ir_new_vreg(context);
  if (pointer < 0) return -1;
  ir_inst_t *alloca = ir_inst_new(IR_ALLOCA);
  if (!alloca) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return -1;
  }
  alloca->dst = ir_val_vreg(pointer, IR_TY_PTR);
  alloca->alloca_size = allocation_size;
  alloca->alloca_align = align;
  if (!hir_ir_append_instruction(context, alloca)) return -1;
  if (over_aligned) {
    int aligned_pointer = hir_ir_new_vreg(context);
    if (aligned_pointer < 0) return -1;
    ir_inst_t *align_pointer = ir_inst_new(IR_ALIGN_PTR);
    if (!align_pointer) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return -1;
    }
    align_pointer->dst = ir_val_vreg(aligned_pointer, IR_TY_PTR);
    align_pointer->src1 = ir_val_vreg(pointer, IR_TY_PTR);
    align_pointer->alloca_align = align;
    if (!hir_ir_append_instruction(context, align_pointer)) return -1;
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
  ir_mir_type_info_t type = hir_ir_classify_node_type(context, local);
  int size = psx_hir_node_object_size(local);
  if (size <= 0) size = type.source_size;
  if (minimum_size > size) size = minimum_size;
  int align = psx_hir_node_object_align(local);
  if (align <= 0)
    align = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
  if (minimum_align > align) align = minimum_align;
  return hir_ir_local_storage_address(context, object_offset, size, align);
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
  int pointer = hir_ir_new_vreg(context);
  if (pointer < 0) return -1;
  ir_inst_t *lea = ir_inst_new(IR_LEA);
  if (!lea) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return -1;
  }
  lea->dst = ir_val_vreg(pointer, IR_TY_PTR);
  lea->src1 = ir_val_vreg(base, IR_TY_PTR);
  lea->src2 = ir_val_imm(IR_TY_I64, delta);
  if (!hir_ir_append_instruction(context, lea)) return -1;
  return pointer;
}

int hir_ir_local_address(
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
    ir_mir_type_info_t source, ir_mir_type_info_t target) {
  target.type = hir_ir_integer_storage_type(target);
  if (!hir_ir_is_integer_type(value.type) ||
      !hir_ir_is_integer_type(target.type))
    return hir_ir_unsupported_expr(context);
  if (value.type == target.type) return value;
  if (value.id == IR_VAL_IMM) {
    value.imm = hir_ir_normalize_integer_immediate(
        value.imm, target.source_size, target.is_unsigned);
    value.type = target.type;
    return value;
  }
  int result = hir_ir_new_vreg(context);
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
  if (!hir_ir_append_instruction(context, conversion)) return ir_val_none();
  return conversion->dst;
}

static ir_val_t coerce_scalar(
    hir_ir_context_t *context, ir_val_t value,
    ir_mir_type_info_t source, ir_mir_type_info_t target) {
  if (!is_scalar_mir_type(source) || !is_scalar_mir_type(target))
    return hir_ir_unsupported_expr(context);
  if (target.type_class == IR_MIR_TYPE_POINTER) {
    if (source.type_class == IR_MIR_TYPE_POINTER &&
        value.type == IR_TY_PTR)
      return value;
    if (source.type_class == IR_MIR_TYPE_INTEGER) {
      int source_size = source.source_size;
      int target_size = ag_target_info_pointer_size(
          context->options->target);
      if (source_size == target_size) {
        value.type = IR_TY_PTR;
        return value;
      }
      int result = hir_ir_new_vreg(context);
      if (result < 0) return ir_val_none();
      ir_inst_t *conversion = ir_inst_new(
          source_size > target_size ? IR_TRUNC : IR_ZEXT);
      if (!conversion) {
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return ir_val_none();
      }
      conversion->dst = ir_val_vreg(result, IR_TY_PTR);
      conversion->src1 = value;
      if (!hir_ir_append_instruction(context, conversion)) return ir_val_none();
      return conversion->dst;
    }
    return hir_ir_unsupported_expr(context);
  }
  if (source.type_class == IR_MIR_TYPE_POINTER) {
    int source_size = ag_target_info_pointer_size(
        context->options->target);
    int target_size = target.source_size;
    if (source_size == target_size) {
      value.type = target.type;
      return value;
    }
    int result = hir_ir_new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *conversion = ir_inst_new(
        source_size > target_size ? IR_TRUNC : IR_ZEXT);
    if (!conversion) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    conversion->dst = ir_val_vreg(result, target.type);
    conversion->src1 = value;
    if (!hir_ir_append_instruction(context, conversion)) return ir_val_none();
    return conversion->dst;
  }
  if (source.type_class != IR_MIR_TYPE_INTEGER)
    return hir_ir_unsupported_expr(context);
  return coerce_integer(context, value, source, target);
}

ir_val_t hir_ir_coerce_direct_value(
    hir_ir_context_t *context, ir_val_t value,
    ir_mir_type_info_t source, ir_mir_type_info_t target) {
  if (!hir_ir_is_scalar_value_type(source) ||
      !hir_ir_is_scalar_value_type(target))
    return hir_ir_unsupported_expr(context);
  if (hir_ir_is_float_value_type(source) || hir_ir_is_float_value_type(target)) {
    if (value.type == target.type) return value;
    ir_op_t op;
    if (hir_ir_is_float_value_type(source) && hir_ir_is_float_value_type(target)) {
      op = IR_F2F;
    } else if (hir_ir_is_float_value_type(source) &&
               target.type_class == IR_MIR_TYPE_INTEGER) {
      op = IR_F2I;
    } else if (source.type_class == IR_MIR_TYPE_INTEGER &&
               hir_ir_is_float_value_type(target)) {
      op = IR_I2F;
    } else {
      return hir_ir_unsupported_expr(context);
    }
    int result = hir_ir_new_vreg(context);
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
    if (!hir_ir_append_instruction(context, conversion)) return ir_val_none();
    return conversion->dst;
  }
  return coerce_scalar(context, value, source, target);
}

ir_val_t hir_ir_coerce_direct_value_to_qual_type(
    hir_ir_context_t *context, ir_val_t value,
    ir_mir_type_info_t source, ir_mir_type_info_t target,
    psx_qual_type_t target_qual_type) {
  const psx_type_t *semantic_target =
      psx_semantic_type_table_lookup(
          context->options->semantic_types,
          target_qual_type.type_id);
  if (semantic_target && semantic_target->kind == PSX_TYPE_BOOL) {
    value = hir_ir_scalar_truth_value(context, value);
    source = (ir_mir_type_info_t){
        .type = IR_TY_I32,
        .type_class = IR_MIR_TYPE_INTEGER,
        .source_size = 4,
        .is_unsigned = 1,
    };
  }
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  return hir_ir_coerce_direct_value(context, value, source, target);
}

static int setup_parameter_bindings(
    hir_ir_context_t *context, const psx_hir_node_t *root,
    const ir_function_type_t *function_type) {
  if (hir_ir_child_count_for_edge(root, PSX_HIR_EDGE_PARAMETER) !=
      function_type->param_count) {
    context->status = IR_HIR_BUILD_INVALID;
    return 0;
  }
  for (size_t i = 0; i < function_type->param_count; i++) {
    const psx_hir_node_t *parameter = hir_ir_child_for_edge(
        context, root, PSX_HIR_EDGE_PARAMETER, i);
    if (!parameter || psx_hir_node_kind(parameter) != PSX_HIR_LOCAL) {
      context->status = IR_HIR_BUILD_INVALID;
      return 0;
    }
    ir_mir_type_info_t type = hir_ir_classify_node_type(context, parameter);
    if ((!hir_ir_is_scalar_value_type(type) &&
         !hir_ir_is_complex_type(type) &&
         type.type_class != IR_MIR_TYPE_AGGREGATE) ||
        function_type->params[i].type_id == PSX_TYPE_ID_INVALID) {
      context->status = IR_HIR_BUILD_UNSUPPORTED;
      return 0;
    }
    int minimum_size =
        (hir_ir_is_complex_type(type) ||
         type.type_class == IR_MIR_TYPE_AGGREGATE)
            ? type.source_size : 0;
    int minimum_align =
        minimum_size >= 8 ? 8 :
        minimum_size >= 4 ? 4 :
        minimum_size >= 2 ? 2 :
        minimum_size > 0 ? 1 : 0;
    int pointer = local_address_with_minimum(
        context, parameter, minimum_size, minimum_align);
    if (pointer < 0) return 0;
    ir_inst_t *binding = ir_inst_new(IR_PARAM_BIND);
    if (!binding) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return 0;
    }
    binding->src1 = ir_val_vreg(pointer, IR_TY_PTR);
    binding->parameter_index = i;
    if (!hir_ir_append_instruction(context, binding)) return 0;
  }
  return 1;
}

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
  int source = find_local_address(context, source_offset);
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

static int emit_vla_parameter_strides(
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
      int source = find_local_address(
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

ir_val_t hir_ir_build_expr(
    hir_ir_context_t *context, const psx_hir_node_t *node);
int hir_ir_build_statement(
    hir_ir_context_t *context, const psx_hir_node_t *node);
ir_val_t hir_ir_emit_integer_binary(
    hir_ir_context_t *context, ir_op_t op, ir_val_t left,
    ir_val_t right, ir_type_t type);
ir_val_t hir_ir_emit_integer_width_conversion(
    hir_ir_context_t *context, ir_val_t value, ir_type_t target,
    int sign_extend);
int hir_ir_allocate_scalar_temp(
    hir_ir_context_t *context, int size, int alignment);

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

const psx_hir_symbol_t *hir_ir_resolved_global_symbol(
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

  ir_mir_type_context_t type_context = {
      .semantic_types = options->semantic_types,
      .record_layouts = options->record_layouts,
      .target = options->target,
  };
  psx_type_id_t signature_id =
      psx_hir_node_attached_qual_type(root).type_id;
  const psx_type_t *function_type = psx_semantic_type_table_lookup(
      options->semantic_types, signature_id);
  psx_type_id_t result_type_id = psx_semantic_type_table_base(
      options->semantic_types, signature_id).type_id;
  ir_mir_type_info_t return_info = ir_mir_classify_type_id(
      &type_context, result_type_id);
  const psx_type_t *result_type = psx_semantic_type_table_lookup(
      options->semantic_types, result_type_id);
  int returns_void = result_type && result_type->kind == PSX_TYPE_VOID;
  if (!function_type || function_type->kind != PSX_TYPE_FUNCTION ||
      function_type->param_count < 0 ||
      (!returns_void && !hir_ir_is_complex_type(return_info) &&
       return_info.type_class != IR_MIR_TYPE_AGGREGATE &&
       !hir_ir_is_direct_value_type(return_info))) {
    if (status) *status = IR_HIR_BUILD_UNSUPPORTED;
    return NULL;
  }
  size_t name_length = 0;
  const char *name = psx_hir_node_name(root, &name_length);
  const psx_hir_node_t *body = hir_ir_child_for_edge(
      &(hir_ir_context_t){.hir = hir}, root,
      PSX_HIR_EDGE_FUNCTION_BODY, 0);
  if (!name || name_length == 0 || !body) {
    return NULL;
  }

  hir_ir_context_t context = {
      .hir = hir,
      .options = options,
      .status = IR_HIR_BUILD_OK,
      .return_info = return_info,
      .return_qual_type = {
          .type_id = result_type_id,
          .qualifiers = PSX_TYPE_QUALIFIER_NONE,
      },
      .returns_void = returns_void,
  };
  context.module = ir_module_new();
  if (!context.module) {
    if (status) *status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return NULL;
  }
  context.function = ir_func_new(
      context.module, name, (int)name_length);
  if (!context.function) {
    ir_module_free(context.module);
    if (status) *status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return NULL;
  }
  if (!ir_function_type_from_type_id(
          options->semantic_types, signature_id,
          &context.function->function_type)) {
    ir_module_free(context.module);
    if (status) *status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return NULL;
  }
  context.function->is_static = psx_hir_node_is_static_function(root);
  if ((size_t)function_type->param_count > INT_MAX) {
    ir_module_free(context.module);
    if (status) *status = IR_HIR_BUILD_UNSUPPORTED;
    return NULL;
  }
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
  if (!setup_parameter_bindings(
          &context, root, &context.function->function_type) ||
      !emit_vla_parameter_strides(&context, root) ||
      !preallocate_local_storage(&context, body) ||
      !hir_ir_cfg_collect_labels(&context, body)) {
    ir_module_free(context.module);
    if (status) *status = context.status;
    return NULL;
  }
  if (!hir_ir_build_statement(&context, body)) {
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
      (!hir_ir_cfg_current_block_terminated(&context) &&
       (context.function->cur_block == context.function->entry ||
        hir_ir_cfg_block_has_predecessor(
            context.function, context.function->cur_block)))) {
    ir_module_free(context.module);
    if (status) *status = IR_HIR_BUILD_UNSUPPORTED;
    return NULL;
  }
  if (status) *status = IR_HIR_BUILD_OK;
  return context.module;
}
