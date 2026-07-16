#ifndef PARSER_NODE_UTILS_H
#define PARSER_NODE_UTILS_H

#include "core.h"
#include "arena.h"
#include "ast.h"
#include "init_slot.h"
#include "gvar_public.h"
#include "../semantic/resolved_node_type.h"
#include "node_vla_public.h"
#include "syntax_node.h"
#include "tag_public.h"
#include "../target_info.h"

struct lvar_t;
struct global_var_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

int ps_node_generic_selection_index(node_generic_selection_t *selection);
psx_gvar_init_slot_t ps_gvar_init_slot_view(const struct global_var_t *gv, int idx);
tk_float_kind_t ps_gvar_init_slot_fp_kind(const struct global_var_t *gv, int idx);
int ps_gvar_init_slot_is_plain_zero(const struct global_var_t *gv, int idx);
int ps_gvar_union_init_slot_fp_size(const struct global_var_t *gv, int idx);
int ps_gvar_union_init_slot_ordinal(const struct global_var_t *gv, int idx);
void ps_gvar_init_slots_alloc(struct global_var_t *gv, int cap, int with_fvalues);
void psx_gvar_init_slots_ensure_capacity(struct global_var_t *gv, int *cap, int min_cap);
void psx_gvar_init_slots_pad_zeros(struct global_var_t *gv, int *cap, int total_slots);
int ps_gvar_init_slots_write_string_units(struct global_var_t *gv, int start_idx,
                                           const char *str, int len,
                                           int elem_size, int max_slots);
void ps_gvar_init_slot_clear(struct global_var_t *gv, int idx);
void ps_gvar_init_slot_write(struct global_var_t *gv, int idx, long long value,
                              double fvalue, char *symbol, int symbol_len);
void ps_gvar_init_slot_write_fp_sentinel(struct global_var_t *gv, int idx,
                                          tk_float_kind_t fp_kind, int fp_size);
void ps_gvar_init_slot_set_ordinal(struct global_var_t *gv, int idx, int ordinal);
int ps_node_is_long_long_type(node_t *node);
int ps_node_is_plain_char_type(node_t *node);
int ps_node_is_long_double_type(node_t *node);
int ps_node_integer_value_is_unsigned(node_t *node);
int ps_node_scalar_ptr_member_lvalue(node_t *node);
int ps_node_subscript_deref_uses_base_address(node_t *node);
const psx_type_t *ps_node_array_decay_pointer_arith_type_in(
    arena_context_t *arena_context, node_t *node);
int ps_node_bitfield_width(node_t *node);

node_t *ps_node_new_binary_for_target_in(
    arena_context_t *arena_context, const ag_target_info_t *target,
    psx_work_node_kind_t kind, node_t *lhs, node_t *rhs);
int ps_node_binary_type_op(
    psx_work_node_kind_t kind, psx_type_binary_op_t *op);
struct psx_vla_runtime_plan_t;
node_t *ps_node_new_vla_runtime_in(
    arena_context_t *arena_context,
    struct psx_vla_runtime_plan_t *runtime_plan);
node_t *ps_node_new_shift_trunc_extend_for_width_in(
    arena_context_t *arena_context, node_t *operand, int left_shift,
    int execution_size, int is_unsigned);
node_t *ps_node_new_num_in(arena_context_t *arena_context, long long val);
node_t *ps_node_new_fp_to_int_cast_in(arena_context_t *arena_context,
                                       node_t *operand,
                                       const psx_type_t *cast_type);
node_t *ps_node_new_int_to_fp_cast_in(arena_context_t *arena_context,
                                       node_t *operand,
                                       const psx_type_t *cast_type);
node_t *ps_node_new_semantic_cast_result_in(
    arena_context_t *arena_context, node_t *operand,
    const psx_type_t *cast_type);
node_t *ps_node_new_integer_cast_result_in(
    arena_context_t *arena_context, node_t *operand,
    const psx_type_t *cast_type);
node_t *ps_node_new_integer_cast_result_ex_in(
    arena_context_t *arena_context, node_t *operand,
    const psx_type_t *cast_type, int widen_zext_i64);
node_t *ps_node_new_i64_to_i32_trunc_cast_in(
    arena_context_t *arena_context, node_t *operand,
    const psx_type_t *cast_type);
node_t *ps_node_new_pointer_cast_result_in(
    arena_context_t *arena_context, node_t *operand,
    const psx_type_t *cast_type);
node_t *ps_node_new_aggregate_cast_result_in(
    arena_context_t *arena_context, node_t *operand,
    const psx_type_t *cast_type);
node_t *ps_node_new_void_cast_result_in(arena_context_t *arena_context,
                                        node_t *operand,
                                        const psx_type_t *cast_type);
node_t *ps_node_new_addr_value_for_in(arena_context_t *arena_context,
                                      node_t *operand);
node_t *ps_node_new_explicit_addr_value_for_in(
    arena_context_t *arena_context, node_t *operand);
node_t *ps_node_new_unary_addr_for_in(arena_context_t *arena_context,
                                      node_t *operand);
node_t *ps_node_new_tag_member_deref_with_layout_for_in(
    arena_context_t *arena_context, const ag_target_info_t *target,
    node_t *addr_base, node_t *base, int member_offset,
    const psx_type_t *member_type, int bit_is_signed,
    int bit_width, int bit_offset);
node_t *ps_node_new_unary_deref_for_in(arena_context_t *arena_context,
                                       node_t *operand);
node_t *ps_node_new_subscript_deref_for_in(
    arena_context_t *arena_context, const ag_target_info_t *target,
    node_t *base, node_t *base_addr, node_t *scaled_offset);
node_t *ps_node_new_assign_in(arena_context_t *arena_context,
                              node_t *lhs, node_t *rhs);
node_t *psx_node_new_raw_decl_initializer_in(
    arena_context_t *arena_context, node_t *target, node_t *value,
    psx_decl_init_kind_t init_kind, token_t *tok);
node_t *psx_node_new_raw_decl_initializer_list_in(
    arena_context_t *arena_context,
    node_t *target, psx_decl_init_kind_t init_kind,
    psx_initializer_entry_t *entries, int entry_count, token_t *tok);
node_t *psx_node_new_initializer_list_in(
    arena_context_t *arena_context,
    psx_initializer_entry_t *entries, int entry_count, token_t *tok);

void ps_node_reject_const_assign_at_in(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const char *op, token_t *tok);
void ps_node_reject_const_qual_discard_at_in(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *lhs, node_t *rhs,
    token_t *tok);
void ps_node_expect_lvalue_at_in(
    ag_diagnostic_context_t *diagnostics, node_t *node,
    const char *op, token_t *tok);

#endif
