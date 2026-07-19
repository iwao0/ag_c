#ifndef LOWERING_HIR_IR_BUILDER_INTERNAL_H
#define LOWERING_HIR_IR_BUILDER_INTERNAL_H

#include "hir_ir_builder.h"
#include "mir_type_lowering.h"
#include "../parser/type.h"

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
  hir_case_target_t *cases;
  size_t case_count;
  size_t case_capacity;
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
  ir_mir_type_info_t return_info;
  psx_qual_type_t return_qual_type;
  const ag_continuation_options_t *continuation;
  const psx_hir_node_t *continuation_while;
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

const psx_hir_node_t *hir_ir_child_for_edge(
    const hir_ir_context_t *context, const psx_hir_node_t *node,
    psx_hir_edge_kind_t edge, size_t occurrence);
size_t hir_ir_child_count_for_edge(
    const psx_hir_node_t *node, psx_hir_edge_kind_t edge);
ir_mir_type_info_t hir_ir_classify_node_type(
    const hir_ir_context_t *context, const psx_hir_node_t *node);
psx_type_kind_t hir_ir_node_type_kind(
    const hir_ir_context_t *context, const psx_hir_node_t *node);
const psx_type_t *hir_ir_node_semantic_type(
    const hir_ir_context_t *context, const psx_hir_node_t *node);
ir_val_t hir_ir_unsupported_expr(hir_ir_context_t *context);
int hir_ir_new_vreg(hir_ir_context_t *context);
int hir_ir_append_instruction(
    hir_ir_context_t *context, ir_inst_t *instruction);
int hir_ir_is_integer_type(ir_type_t type);
int hir_ir_is_float_type(ir_type_t type);
int hir_ir_is_float_value_type(ir_mir_type_info_t type);
int hir_ir_is_complex_type(ir_mir_type_info_t type);
int hir_ir_is_scalar_value_type(ir_mir_type_info_t type);
int hir_ir_is_direct_value_type(ir_mir_type_info_t type);
ir_type_t hir_ir_integer_storage_type(ir_mir_type_info_t type);
ir_type_t hir_ir_scalar_storage_type(ir_mir_type_info_t type);
long long hir_ir_normalize_integer_immediate(
    long long value, int byte_size, int is_unsigned);
int hir_ir_local_storage_address(
    hir_ir_context_t *context, int object_offset, int size, int align);
int hir_ir_find_local_address(
    const hir_ir_context_t *context, int object_offset);
int hir_ir_local_address(
    hir_ir_context_t *context, const psx_hir_node_t *local);
int hir_ir_local_address_with_minimum(
    hir_ir_context_t *context, const psx_hir_node_t *local,
    int minimum_size, int minimum_align);
ir_val_t hir_ir_coerce_direct_value(
    hir_ir_context_t *context, ir_val_t value,
    ir_mir_type_info_t source, ir_mir_type_info_t target);
int hir_ir_emit_conditional_branch(
    hir_ir_context_t *context, ir_val_t condition,
    ir_block_t *if_true, ir_block_t *if_false);
ir_val_t hir_ir_build_expr(
    hir_ir_context_t *context, const psx_hir_node_t *node);
ir_val_t hir_ir_build_call(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t result_type);
int hir_ir_setup_parameter_bindings(
    hir_ir_context_t *context, const psx_hir_node_t *root,
    const ir_function_type_t *function_type);
ir_val_t hir_ir_build_aggregate_assignment(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t target_type);
ir_val_t hir_ir_build_object_copy(
    hir_ir_context_t *context, const psx_hir_node_t *node);
int hir_ir_build_statement(
    hir_ir_context_t *context, const psx_hir_node_t *node);
ir_val_t hir_ir_build_vla_allocation(
    hir_ir_context_t *context, const psx_hir_node_t *node);
int hir_ir_emit_vla_parameter_strides(
    hir_ir_context_t *context, const psx_hir_node_t *root);
ir_val_t hir_ir_emit_integer_binary(
    hir_ir_context_t *context, ir_op_t op, ir_val_t left,
    ir_val_t right, ir_type_t type);
ir_val_t hir_ir_emit_integer_width_conversion(
    hir_ir_context_t *context, ir_val_t value, ir_type_t target,
    int sign_extend);
int hir_ir_allocate_scalar_temp(
    hir_ir_context_t *context, int size, int alignment);
ir_val_t hir_ir_scalar_truth_value(
    hir_ir_context_t *context, ir_val_t value);
ir_val_t hir_ir_pointer_with_offset(
    hir_ir_context_t *context, ir_val_t base, int offset);
int hir_ir_store_direct_value(
    hir_ir_context_t *context, ir_val_t pointer, ir_val_t value);
int hir_ir_node_is_lvalue(const psx_hir_node_t *node);
ir_val_t hir_ir_lvalue_address(
    hir_ir_context_t *context, const psx_hir_node_t *node);
const psx_hir_symbol_t *hir_ir_resolved_global_symbol(
    hir_ir_context_t *context, const psx_hir_node_t *node);
ir_val_t hir_ir_materialize_complex_operand(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t target_type);
ir_val_t hir_ir_aggregate_value_address(
    hir_ir_context_t *context, const psx_hir_node_t *node);
ir_val_t hir_ir_coerce_direct_value_to_qual_type(
    hir_ir_context_t *context, ir_val_t value,
    ir_mir_type_info_t source, ir_mir_type_info_t target,
    psx_qual_type_t target_qual_type);

int hir_ir_cfg_current_block_terminated(const hir_ir_context_t *context);
int hir_ir_cfg_block_has_predecessor(
    const ir_func_t *function, const ir_block_t *target);
ir_block_t *hir_ir_cfg_new_block(hir_ir_context_t *context);
int hir_ir_cfg_switch_to_block(
    hir_ir_context_t *context, ir_block_t *block);
int hir_ir_cfg_emit_branch(
    hir_ir_context_t *context, ir_block_t *target);
int hir_ir_cfg_push_loop(
    hir_ir_context_t *context, ir_block_t *continue_block,
    ir_block_t *break_block);
void hir_ir_cfg_pop_loop(hir_ir_context_t *context);
ir_block_t *hir_ir_cfg_lookup_label(
    const hir_ir_context_t *context, const char *name,
    size_t name_length);
int hir_ir_cfg_collect_labels(
    hir_ir_context_t *context, const psx_hir_node_t *node);

#endif
