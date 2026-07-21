#ifndef PARSER_NODE_UTILS_LEGACY_H
#define PARSER_NODE_UTILS_LEGACY_H

#include "node_utils.h"
#include "type.h"
#include "../target_info.h"

const psx_type_t *ps_node_array_decay_pointer_arith_type_in(
    const psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node);
node_t *ps_node_new_binary_for_data_layout_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    const ag_data_layout_t *data_layout, psx_resolution_node_kind_t kind,
    node_t *lhs, node_t *rhs);
int ps_node_binary_type_op(
    psx_resolution_node_kind_t kind, psx_type_binary_op_t *op);
node_t *ps_node_new_shift_trunc_extend_for_width_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, int left_shift,
    int execution_size, int is_unsigned);
node_t *ps_node_new_num_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, long long val);
node_t *ps_node_new_fp_to_int_cast_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type);
node_t *ps_node_new_int_to_fp_cast_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type);
node_t *ps_node_new_semantic_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type);
node_t *ps_node_new_integer_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type);
node_t *ps_node_new_integer_cast_result_ex_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type, int widen_zext_i64);
node_t *ps_node_new_i64_to_i32_trunc_cast_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type);
node_t *ps_node_new_pointer_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type);
node_t *ps_node_new_aggregate_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type);
node_t *ps_node_new_void_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type);
node_t *ps_node_new_addr_value_for_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand);
node_t *ps_node_new_explicit_addr_value_for_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand);
node_t *ps_node_new_unary_addr_for_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand);
node_t *ps_node_new_tag_member_deref_with_layout_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, const ag_target_info_t *target,
    node_t *addr_base, node_t *base, int member_offset,
    const psx_type_t *member_type, int bit_is_signed,
    int bit_width, int bit_offset);
node_t *ps_node_new_unary_deref_for_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand);
node_t *ps_node_new_subscript_deref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, const ag_target_info_t *target,
    node_t *base, node_t *base_addr, node_t *scaled_offset);
node_t *ps_node_new_assign_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *lhs, node_t *rhs);

#endif
