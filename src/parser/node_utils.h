#ifndef PARSER_NODE_UTILS_H
#define PARSER_NODE_UTILS_H

#include "core.h"
#include "ast.h"
#include "init_slot.h"
#include "gvar_public.h"
#include "node_funcptr_public.h"
#include "node_type_public.h"
#include "node_vla_public.h"
#include "tag_public.h"

struct lvar_t;
struct global_var_t;
struct tag_member_info_t;

void ps_node_bind_type(node_t *node, psx_type_t *type);
unsigned short psx_node_funcptr_param_fp_mask(node_t *node);
unsigned short psx_node_funcptr_param_int_mask(node_t *node);
int psx_node_has_funcptr_signature(node_t *node);
int psx_node_funcptr_returns_void(node_t *node);
int psx_node_funcptr_returns_complex(node_t *node);
int psx_node_funcptr_returns_pointee_array(node_t *node);
tk_float_kind_t psx_node_funcptr_ret_fp_kind(node_t *node);
int ps_node_generic_selection_index(node_generic_selection_t *selection);
psx_type_t *ps_lvar_get_decl_type(struct lvar_t *var);
psx_type_t *ps_gvar_get_decl_type(struct global_var_t *gv);
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
int ps_node_base_deref_size(node_t *node);
int ps_node_ptr_array_pointee_bytes(node_t *node);
unsigned int ps_node_pointer_const_qual_mask(node_t *node);
unsigned int ps_node_pointer_volatile_qual_mask(node_t *node);
int ps_node_pointee_is_unsigned(node_t *node);
int ps_node_pointee_is_bool(node_t *node);
int ps_node_pointee_is_void(node_t *node);
int ps_node_pointee_is_const_qualified(node_t *node);
int ps_node_pointee_is_volatile_qualified(node_t *node);
int ps_node_is_long_long_type(node_t *node);
int ps_node_is_plain_char_type(node_t *node);
int ps_node_is_long_double_type(node_t *node);
int ps_node_integer_value_is_unsigned(node_t *node);
void ps_node_set_unsigned(node_t *node, int is_unsigned);
tk_float_kind_t ps_node_pointee_fp_kind(node_t *node);
int ps_node_pointer_stride_metadata(node_t *node, int *inner_stride,
                                     int *next_stride, int *extra_strides,
                                     int *extra_strides_count);
int ps_node_scalar_ptr_member_lvalue(node_t *node);
int ps_node_subscript_deref_uses_base_address(node_t *node);
psx_type_t *ps_node_row_decay_pointer_arith_type(node_t *node);
int ps_node_bitfield_width(node_t *node);
/* Canonical node typeに紐付くtagの宣言時scope depth。 */
int ps_node_get_tag_scope_depth(node_t *node);

node_t *ps_node_new_binary(node_kind_t kind, node_t *lhs, node_t *rhs);
node_t *psx_node_new_raw_binary(node_kind_t kind, node_t *lhs, node_t *rhs);
int ps_node_binary_type_op(
    node_kind_t kind, psx_type_binary_op_t *op);
node_t *ps_node_new_vla_alloc(int descriptor_frame_off,
                               int row_stride_frame_off,
                               node_t *lhs, node_t *rhs);
node_t *ps_node_new_shift_trunc_extend(node_t *operand, int left_shift, int is_unsigned);
node_t *ps_node_new_num(long long val);
node_t *psx_node_new_lvar(int offset);
node_t *ps_node_new_lvar_typed(int offset, int type_size);
node_t *ps_node_new_lvar_typed_at_for(struct lvar_t *owner, int offset, int type_size);
node_t *ps_node_new_lvar_type_at_for(struct lvar_t *owner, int offset,
                                      psx_type_t *type);
node_t *psx_node_new_lvar_scalar_slot_at(int offset, int type_size,
                                         tk_float_kind_t fp_kind, int is_bool);
node_t *psx_node_new_lvar_fp_slot_at(int offset, int type_size, tk_float_kind_t fp_kind);
node_t *ps_node_new_lvar_fp_slot_for(struct lvar_t *owner, int offset, int type_size);
node_t *ps_node_new_param_placeholder(psx_type_t *type);
node_t *ps_node_new_unsigned_lvar_typed(int offset, int type_size);
node_t *psx_node_new_lvar_for(struct lvar_t *var);
node_t *psx_node_new_lvar_typed_for(struct lvar_t *var, int type_size);
node_t *psx_node_new_lvar_object_ref_for(struct lvar_t *var);
node_t *ps_node_new_lvar_expr_ref_for(struct lvar_t *var, int is_pointer);
node_t *psx_node_new_lvar_identifier_ref_for(struct lvar_t *var);
node_t *psx_node_new_vla_decay_ref_for(struct lvar_t *var);
node_t *ps_node_new_param_lvar_for(struct lvar_t *var);
node_t *ps_node_new_array_elem_lvar_for(struct lvar_t *var, int idx);
node_t *ps_node_new_fp_to_int_cast(node_t *operand, int width, psx_type_t *cast_type);
node_t *ps_node_new_int_to_fp_cast(node_t *operand, tk_float_kind_t target,
                                    psx_type_t *cast_type);
node_t *ps_node_new_integer_cast_result(node_t *operand, psx_type_t *cast_type,
                                         int type_size, int is_unsigned,
                                         int is_long_long);
node_t *ps_node_new_integer_cast_result_ex(node_t *operand, psx_type_t *cast_type,
                                            int type_size, int is_unsigned,
                                            int is_long_long, int is_plain_char,
                                            int widen_zext_i64);
node_t *ps_node_new_i64_to_i32_trunc_cast(node_t *operand, psx_type_t *cast_type,
                                           int is_unsigned);
node_t *ps_node_new_pointer_cast_result(node_t *operand, psx_type_t *cast_type,
                                         token_kind_t type_kind,
                                         token_kind_t tag_kind,
                                         char *tag_name, int tag_len,
                                         int elem_size, int is_unsigned);
node_t *ps_node_new_aggregate_cast_result(node_t *operand, psx_type_t *cast_type);
node_t *ps_node_new_void_cast_result(node_t *operand, psx_type_t *cast_type);
node_t *psx_node_new_source_cast(
    node_t *operand, psx_type_name_ref_t type_name);
node_t *ps_node_new_gvar_array_addr_for(struct global_var_t *gv);
node_t *psx_node_new_static_local_array_addr_for(struct lvar_t *var, int gvar_type_size);
node_t *ps_node_new_lvar_array_addr_for(struct lvar_t *var, int is_tag_pointer);
node_t *ps_node_new_addr_value_for(node_t *operand);
node_t *ps_node_new_explicit_addr_value_for(node_t *operand);
node_t *ps_node_new_unary_addr_for(node_t *operand);
node_t *ps_node_new_tag_member_deref_for(node_t *addr_base, node_t *base,
                                          const struct tag_member_info_t *info);
node_t *ps_node_new_unary_deref_for(node_t *operand);
node_t *psx_node_new_unary_deref_syntax_for(node_t *operand);
node_t *psx_node_new_subscript_syntax_for(node_t *base, node_t *index);
node_t *ps_node_new_subscript_deref_for(node_t *base, node_t *base_addr,
                                         node_t *scaled_offset,
                                         int elem_size, int inner_deref_size,
                                         int next_deref_size,
                                         const int *extra_strides,
                                         int extra_strides_count);
node_t *psx_node_new_member_lvar_ref_for(struct lvar_t *owner, int member_offset,
                                         int member_type_size, token_kind_t member_tag_kind,
                                         char *member_tag_name, int member_tag_len,
                                         int member_is_tag_pointer);
node_t *ps_node_new_tag_member_lvar_ref_for(struct lvar_t *owner, int member_offset,
                                             const struct tag_member_info_t *info);
node_t *ps_node_new_gvar_for(struct global_var_t *gv);
node_t *psx_node_new_gvar_array_base_for(struct global_var_t *gv);
node_t *psx_node_new_static_local_gvar_for(struct lvar_t *var, int type_size);
struct lvar_t *ps_node_lvar_symbol(node_t *node);
node_t *ps_node_clone_lvalue_with_lhs(node_t *target, node_t *lhs);
node_t *ps_node_new_assign(node_t *lhs, node_t *rhs);
node_t *psx_node_new_raw_assign(node_t *lhs, node_t *rhs);
node_t *psx_node_new_raw_decl_initializer(node_t *target, node_t *value,
                                          psx_decl_init_kind_t init_kind,
                                          token_t *tok);
node_t *psx_node_new_compound_literal(
    psx_type_name_ref_t type_name, node_t *initializer, token_t *tok,
    int requires_addressable_object, int has_file_scope_storage);
node_t *psx_node_new_raw_decl_initializer_list(
    node_t *target, psx_decl_init_kind_t init_kind,
    psx_initializer_entry_t *entries, int entry_count, token_t *tok);
node_t *psx_node_new_initializer_list(
    psx_initializer_entry_t *entries, int entry_count, token_t *tok);

void psx_node_reject_const_assign(node_t *node, const char *op);
void ps_node_reject_const_assign_at(node_t *node, const char *op,
                                     token_t *tok);
void psx_node_reject_const_qual_discard(node_t *lhs, node_t *rhs);
void ps_node_reject_const_qual_discard_at(node_t *lhs, node_t *rhs,
                                           token_t *tok);
void ps_node_expect_lvalue_at(node_t *node, const char *op, token_t *tok);
void psx_node_expect_lvalue(node_t *node, const char *op);

#endif
int ps_node_compound_literal_array_size(node_t *node);
