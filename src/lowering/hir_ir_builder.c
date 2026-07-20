#include "hir_ir_builder.h"
#include "hir_ir_builder_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir_type_lowering.h"
#include "function_type_lowering.h"
#include "../diag/diag.h"
#include "../semantic/type_identity.h"
#include "../type_layout.h"
#include "../type_signature.h"

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
  psx_type_shape_t type = {0};
  return hir_ir_node_type_shape(context, node, &type)
             ? type.kind : PSX_TYPE_INVALID;
}

int hir_ir_type_shape(
    const hir_ir_context_t *context, psx_type_id_t type_id,
    psx_type_shape_t *out) {
  return context && context->options && out &&
         psx_semantic_type_table_describe(
             context->options->semantic_types, type_id, out);
}

int hir_ir_node_type_shape(
    const hir_ir_context_t *context, const psx_hir_node_t *node,
    psx_type_shape_t *out) {
  return node && hir_ir_type_shape(
                     context, psx_hir_node_qual_type(node).type_id, out);
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

int hir_ir_find_local_address(
    const hir_ir_context_t *context, int object_offset) {
  for (size_t i = 0; i < context->local_slot_count; i++) {
    if (context->local_slots[i].object_offset == object_offset)
      return context->local_slots[i].pointer_vreg;
  }
  return -1;
}

int hir_ir_local_storage_address(
    hir_ir_context_t *context, int object_offset, int size, int align) {
  int existing = hir_ir_find_local_address(context, object_offset);
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

int hir_ir_local_address_with_minimum(
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
  return hir_ir_local_address_with_minimum(context, local, 0, 0);
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
  psx_type_shape_t semantic_target = {0};
  if (hir_ir_type_shape(
          context, target_qual_type.type_id, &semantic_target) &&
      semantic_target.kind == PSX_TYPE_BOOL) {
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

static int hir_name_matches(
    const char *name, size_t name_length, const char *expected) {
  return name && expected && name_length == strlen(expected) &&
         memcmp(name, expected, name_length) == 0;
}

static int exact_int_void_function(
    const psx_semantic_type_table_t *types, psx_qual_type_t qual_type) {
  psx_qual_type_t function_type =
      psx_semantic_type_table_callable_function(types, qual_type);
  psx_qual_type_t result_qual_type = psx_semantic_type_table_base(
      types, function_type.type_id);
  psx_type_shape_t function = {0};
  psx_type_shape_t result = {0};
  return psx_semantic_type_table_describe(
             types, function_type.type_id, &function) &&
         psx_semantic_type_table_describe(
             types, result_qual_type.type_id, &result) &&
         function.kind == PSX_TYPE_FUNCTION &&
         function.parameter_count == 0 &&
         !function.is_variadic_function &&
         result.kind == PSX_TYPE_INTEGER &&
         result.integer_kind == PSX_INTEGER_KIND_INT &&
         !result.is_unsigned;
}

typedef struct {
  const hir_ir_context_t *context;
  const ag_continuation_options_t *options;
  const psx_hir_node_t *frame_while;
  const psx_hir_node_t *invalid_node;
  const psx_hir_node_t *frame_invalid_node;
  diag_error_id_t invalid_reason;
  diag_error_id_t frame_invalid_reason;
  int frame_while_count;
  int condition_call_count;
} hir_continuation_scan_t;

static void scan_continuation_node(
    const psx_hir_node_t *node, hir_continuation_scan_t *scan) {
  if (!node || scan->invalid_node) return;
  psx_hir_node_kind_t kind = psx_hir_node_kind(node);
  if (kind == PSX_HIR_GOTO || kind == PSX_HIR_LABEL) {
    if (!scan->frame_invalid_node) {
      scan->frame_invalid_node = node;
      scan->frame_invalid_reason =
          DIAG_ERR_PARSER_CONTINUATION_GOTO_LABEL_ACROSS_FRAMES;
    }
    return;
  }
  if (kind == PSX_HIR_VLA_ALLOC) {
    if (!scan->frame_invalid_node) {
      scan->frame_invalid_node = node;
      scan->frame_invalid_reason =
          DIAG_ERR_PARSER_CONTINUATION_VLA_ACROSS_FRAMES;
    }
    return;
  }
  if (kind == PSX_HIR_CALL) {
    size_t name_length = 0;
    const char *name = psx_hir_node_name(node, &name_length);
    if (hir_name_matches(
            name, name_length, scan->options->frame_condition)) {
      scan->condition_call_count++;
      if (!exact_int_void_function(
              scan->context->options->semantic_types,
              psx_hir_node_attached_qual_type(node))) {
        scan->invalid_node = node;
        scan->invalid_reason =
            DIAG_ERR_PARSER_CONTINUATION_FRAME_CONDITION_TYPE;
        return;
      }
    }
    if ((hir_name_matches(name, name_length, "alloca") ||
         hir_name_matches(name, name_length, "__builtin_alloca")) &&
        !scan->frame_invalid_node) {
      scan->frame_invalid_node = node;
      scan->frame_invalid_reason =
          DIAG_ERR_PARSER_CONTINUATION_ALLOCA_ACROSS_FRAMES;
    }
  }
  if (kind == PSX_HIR_WHILE) {
    const psx_hir_node_t *condition = hir_ir_child_for_edge(
        scan->context, node, PSX_HIR_EDGE_LHS, 0);
    size_t name_length = 0;
    const char *name = condition &&
            psx_hir_node_kind(condition) == PSX_HIR_CALL
        ? psx_hir_node_name(condition, &name_length) : NULL;
    if (hir_name_matches(
            name, name_length, scan->options->frame_condition)) {
      scan->frame_while = node;
      scan->frame_while_count++;
    }
  }
  for (size_t i = 0; i < psx_hir_node_child_count(node); i++) {
    const psx_hir_node_t *child = psx_hir_module_lookup(
        scan->context->hir, psx_hir_node_child_at(node, i));
    scan_continuation_node(child, scan);
  }
}

static int emit_continuation_error(
    hir_ir_context_t *context, diag_error_id_t id) {
  ag_diagnostic_context_t *diagnostics =
      context->options->diagnostic_context;
  diag_emit_tokf_in(
      diagnostics, id, NULL, "%s",
      diag_message_for_in(diagnostics, id));
  context->status = IR_HIR_BUILD_INVALID;
  return 0;
}

static int prepare_continuation_entry(
    hir_ir_context_t *context, const psx_hir_node_t *root,
    const psx_hir_node_t *body) {
  const ag_continuation_options_t *options =
      context->options->continuation;
  if (!options) return 1;
  size_t name_length = 0;
  const char *name = psx_hir_node_name(root, &name_length);
  if (!hir_name_matches(name, name_length, options->entry) ||
      psx_hir_node_is_static_function(root))
    return 1;
  if (!exact_int_void_function(
          context->options->semantic_types,
          psx_hir_node_attached_qual_type(root)))
    return emit_continuation_error(
        context, DIAG_ERR_PARSER_CONTINUATION_ENTRY_TYPE);

  hir_continuation_scan_t scan = {
      .context = context,
      .options = options,
  };
  scan_continuation_node(body, &scan);
  if (scan.invalid_node)
    return emit_continuation_error(context, scan.invalid_reason);
  int synchronous =
      scan.frame_while_count == 0 && scan.condition_call_count == 0;
  int frame_continuation =
      scan.frame_while_count == 1 && scan.condition_call_count == 1;
  if (!synchronous && !frame_continuation) {
    return emit_continuation_error(
        context,
        scan.frame_while_count == 0
            ? DIAG_ERR_PARSER_CONTINUATION_FRAME_LOOP_REQUIRED
            : DIAG_ERR_PARSER_CONTINUATION_FRAME_CONDITION_CALL_COUNT);
  }
  if (frame_continuation && scan.frame_invalid_node)
    return emit_continuation_error(
        context, scan.frame_invalid_reason);
  context->continuation = options;
  context->continuation_while = scan.frame_while;
  return 1;
}

static char *duplicate_string(const char *text) {
  if (!text) return NULL;
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (copy) memcpy(copy, text, length + 1);
  return copy;
}

static int set_continuation_function_metadata(
    hir_ir_context_t *context) {
  ir_func_t *function = context->function;
  const ag_continuation_options_t *options = context->continuation;
  if (!function || !options) return 0;
  function->is_continuation_entry = 1;
  function->continuation_has_suspend =
      context->continuation_while ? 1 : 0;
  function->continuation_entry_name = duplicate_string(options->entry);
  function->continuation_condition_name =
      duplicate_string(options->frame_condition);
  function->continuation_start_export =
      duplicate_string(options->start_export);
  function->continuation_resume_export =
      duplicate_string(options->resume_export);
  function->continuation_status_export =
      duplicate_string(options->status_export);
  function->continuation_result_export =
      duplicate_string(options->result_export);
  if (!function->continuation_entry_name ||
      !function->continuation_condition_name ||
      !function->continuation_start_export ||
      !function->continuation_resume_export ||
      !function->continuation_status_export ||
      !function->continuation_result_export) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return 0;
  }
  return 1;
}



ir_module_t *ir_build_function_module_from_hir(
    const psx_hir_module_t *hir, psx_hir_node_id_t function_root,
    const ir_build_options_t *options, ir_hir_build_status_t *status) {
  if (status) *status = IR_HIR_BUILD_INVALID;
  if (!hir || !options || !options->target || !options->semantic_types ||
      !options->record_layouts) {
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
  psx_type_shape_t function_type = {0};
  psx_type_id_t result_type_id = psx_semantic_type_table_base(
      options->semantic_types, signature_id).type_id;
  ir_mir_type_info_t return_info = ir_mir_classify_type_id(
      &type_context, result_type_id);
  psx_type_shape_t result_type = {0};
  int has_function_type = psx_semantic_type_table_describe(
      options->semantic_types, signature_id, &function_type);
  int has_result_type = psx_semantic_type_table_describe(
      options->semantic_types, result_type_id, &result_type);
  int returns_void = has_result_type && result_type.kind == PSX_TYPE_VOID;
  if (!has_function_type || function_type.kind != PSX_TYPE_FUNCTION ||
      function_type.parameter_count < 0 || !has_result_type ||
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
  if (!prepare_continuation_entry(&context, root, body)) {
    if (status) *status = context.status;
    return NULL;
  }
  context.module = ir_module_new_with_allocation_stats(
      options->allocation_stats);
  if (!context.module) {
    if (status) *status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return NULL;
  }
  char *continuation_name = NULL;
  const char *ir_name = name;
  size_t ir_name_length = name_length;
  if (context.continuation) {
    int length = snprintf(
        NULL, 0, "__agc_continuation_step_%.*s",
        (int)name_length, name);
    if (length < 0) {
      ir_module_free(context.module);
      if (status) *status = IR_HIR_BUILD_INVALID;
      return NULL;
    }
    continuation_name = malloc((size_t)length + 1);
    if (!continuation_name) {
      ir_module_free(context.module);
      if (status) *status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return NULL;
    }
    snprintf(
        continuation_name, (size_t)length + 1,
        "__agc_continuation_step_%.*s", (int)name_length, name);
    ir_name = continuation_name;
    ir_name_length = (size_t)length;
  }
  context.function = ir_func_new(
      context.module, ir_name, (int)ir_name_length);
  free(continuation_name);
  if (!context.function) {
    ir_module_free(context.module);
    if (status) *status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return NULL;
  }
  psx_qual_type_t continuation_parameter = {
      .type_id = result_type_id,
      .qualifiers = PSX_TYPE_QUALIFIER_NONE,
  };
  int function_type_lowered = context.continuation
      ? ir_function_type_set(
            &context.function->function_type, PSX_TYPE_ID_INVALID,
            continuation_parameter, &continuation_parameter, 1, 0, 1)
      : ir_function_type_from_type_id(
            options->semantic_types, signature_id,
            &context.function->function_type);
  if (!function_type_lowered ||
      (context.continuation &&
       !set_continuation_function_metadata(&context))) {
    ir_module_free(context.module);
    if (status) *status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return NULL;
  }
  context.function->is_static = psx_hir_node_is_static_function(root);
  int signature_length =
      context.continuation
          ? 0
          : psx_format_canonical_type_signature(
                options->semantic_types,
                (psx_qual_type_t){signature_id, PSX_TYPE_QUALIFIER_NONE},
                ag_target_info_data_layout(options->target), NULL, 0);
  if (signature_length < 0) {
    ir_module_free(context.module);
    if (status) *status = IR_HIR_BUILD_INVALID;
    return NULL;
  }
  if (!context.continuation) {
    context.function->c_signature = malloc((size_t)signature_length + 1);
    if (!context.function->c_signature ||
        psx_format_canonical_type_signature(
            options->semantic_types,
            (psx_qual_type_t){signature_id, PSX_TYPE_QUALIFIER_NONE},
            ag_target_info_data_layout(options->target),
            context.function->c_signature,
            (size_t)signature_length + 1) != signature_length) {
      ir_module_free(context.module);
      if (status) *status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return NULL;
    }
    context.function->c_signature_len = signature_length;
  }
  if ((!context.continuation && !hir_ir_setup_parameter_bindings(
          &context, root, &context.function->function_type)) ||
      !hir_ir_emit_vla_parameter_strides(&context, root) ||
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
