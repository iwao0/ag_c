#ifndef PARSER_NODE_UTILS_H
#define PARSER_NODE_UTILS_H

#include "core.h"
#include "ast.h"

struct lvar_t;
struct global_var_t;
struct tag_member_info_t;

psx_type_t *psx_node_get_type(node_t *node);
psx_type_t *psx_node_materialize_type(node_t *node);
psx_type_t *psx_lvar_get_decl_type(struct lvar_t *var);
psx_type_t *psx_lvar_materialize_decl_type(struct lvar_t *var);
psx_type_t *psx_gvar_get_decl_type(struct global_var_t *gv);
psx_type_t *psx_gvar_materialize_decl_type(struct global_var_t *gv);

int ps_node_type_size(node_t *node);
int ps_node_deref_size(node_t *node);
int ps_node_is_pointer(node_t *node);
int psx_node_pointer_qual_levels(node_t *node);
int psx_node_base_deref_size(node_t *node);
int psx_node_ptr_array_pointee_bytes(node_t *node);
unsigned short psx_node_funcptr_param_fp_mask(node_t *node);
unsigned short psx_node_funcptr_param_int_mask(node_t *node);
int psx_node_funcptr_returns_void(node_t *node);
int psx_node_funcptr_returns_complex(node_t *node);
int psx_node_funcptr_returns_pointee_array(node_t *node);
tk_float_kind_t psx_node_funcptr_ret_fp_kind(node_t *node);
int psx_node_mem_has_funcptr_metadata(const node_mem_t *mem);
void psx_node_store_funcptr_metadata(node_mem_t *dst, psx_decl_funcptr_sig_t sig);
psx_decl_funcptr_sig_t psx_node_funcdef_ret_funcptr_sig(const node_func_t *fn);
void psx_node_funcdef_set_ret_funcptr_sig(node_func_t *fn, psx_decl_funcptr_sig_t sig);
void psx_node_copy_funcptr_metadata(node_mem_t *dst, node_t *src);
void psx_node_copy_funcptr_metadata_from_lvar(node_mem_t *dst, const struct lvar_t *src);
void psx_node_copy_funcptr_metadata_from_gvar(node_mem_t *dst, const struct global_var_t *src);
void psx_node_merge_funcptr_metadata_from_lvar(node_mem_t *dst, const struct lvar_t *src);
void psx_node_merge_funcptr_metadata_from_gvar(node_mem_t *dst, const struct global_var_t *src);
void psx_node_copy_funcptr_metadata_from_tag_member(node_mem_t *dst,
                                                    const struct tag_member_info_t *src);
void psx_node_init_gvar_ref_metadata(node_mem_t *mem, const struct global_var_t *gv);
void psx_node_init_gvar_array_base_metadata(node_mem_t *mem, const struct global_var_t *gv);
void psx_node_init_static_local_gvar_ref_metadata(node_mem_t *mem, const struct lvar_t *var,
                                                  int type_size);
void psx_node_init_lvar_array_addr_metadata(node_mem_t *addr, const struct lvar_t *var,
                                            int is_tag_pointer);
void psx_node_init_gvar_array_addr_metadata(node_mem_t *addr, const struct global_var_t *gv);
void psx_node_init_compound_lvar_array_addr_metadata(node_mem_t *addr, const struct lvar_t *var,
                                                     token_kind_t tag_kind, char *tag_name,
                                                     int tag_len, int array_size);
void psx_node_init_compound_gvar_array_addr_metadata(node_mem_t *addr, const struct global_var_t *gv,
                                                     int ptr_array_pointee_bytes,
                                                     int pointer_elem_size, int array_size);
unsigned int psx_node_pointer_const_qual_mask(node_t *node);
unsigned int psx_node_pointer_volatile_qual_mask(node_t *node);
int psx_node_pointee_is_unsigned(node_t *node);
int psx_node_pointee_is_bool(node_t *node);
int psx_node_pointee_is_void(node_t *node);
int psx_node_pointee_is_const_qualified(node_t *node);
int psx_node_pointee_is_volatile_qualified(node_t *node);
int psx_node_is_unsigned_type(node_t *node);
int psx_node_is_long_long_type(node_t *node);
int psx_node_is_plain_char_type(node_t *node);
int psx_node_is_long_double_type(node_t *node);
int psx_node_integer_value_is_unsigned(node_t *node);
int psx_node_integer_promotion_is_unsigned(node_t *node);
int psx_node_conversion_value_is_unsigned(node_t *node);
int psx_node_i64_widen_source_is_unsigned(node_t *node);
int psx_node_shift_operation_is_unsigned(node_t *node);
int psx_node_usual_arith_operands_is_unsigned(node_t *lhs, node_t *rhs);
int psx_node_usual_arith_is_unsigned(node_t *node);
void psx_node_set_unsigned(node_t *node, int is_unsigned);
tk_float_kind_t psx_node_pointee_fp_kind(node_t *node);
int psx_node_vla_row_stride_frame_off(node_t *node);
void psx_node_get_tag_type(node_t *node, token_kind_t *tag_kind, char **tag_name, int *tag_len, int *is_tag_pointer);
/* タグ shadow 応用形向け: ノードに紐付くタグの宣言時 scope_depth を返す (見つからなければ -1)。
 * build_member_access が「変数が宣言時に見ていた tag」のメンバを引くのに使う。 */
int psx_node_get_tag_scope_depth(node_t *node);

node_t *psx_node_new_binary(node_kind_t kind, node_t *lhs, node_t *rhs);
node_t *psx_node_new_shift_trunc_extend(node_t *operand, int left_shift, int is_unsigned);
node_t *psx_node_new_num(long long val);
node_t *psx_node_new_lvar(int offset);
node_t *psx_node_new_lvar_typed(int offset, int type_size);
node_t *psx_node_new_lvar_typed_at_for(struct lvar_t *owner, int offset, int type_size);
node_t *psx_node_new_lvar_scalar_slot_at(int offset, int type_size,
                                         tk_float_kind_t fp_kind, int is_bool);
node_t *psx_node_new_lvar_fp_slot_at(int offset, int type_size, tk_float_kind_t fp_kind);
node_t *psx_node_new_lvar_fp_slot_for(struct lvar_t *owner, int offset, int type_size);
node_t *psx_node_new_param_placeholder(int is_pointer, tk_float_kind_t fp_kind, int is_unsigned);
node_t *psx_node_new_unsigned_lvar_typed(int offset, int type_size);
node_t *psx_node_new_lvar_for(struct lvar_t *var);
node_t *psx_node_new_lvar_typed_for(struct lvar_t *var, int type_size);
node_t *psx_node_new_lvar_object_ref_for(struct lvar_t *var);
node_t *psx_node_new_lvar_expr_ref_for(struct lvar_t *var, int is_pointer);
node_t *psx_node_new_lvar_identifier_ref_for(struct lvar_t *var);
node_t *psx_node_new_param_lvar_for(struct lvar_t *var, int abi_type_size,
                                    int is_unsigned, tk_float_kind_t abi_fp_kind,
                                    int is_complex);
node_t *psx_node_new_array_elem_lvar_for(struct lvar_t *var, int idx);
node_t *psx_node_new_fp_to_int_cast(node_t *operand, int width, psx_type_t *cast_type);
node_t *psx_node_new_int_to_fp_cast(node_t *operand, tk_float_kind_t target,
                                    psx_type_t *cast_type);
node_t *psx_node_new_integer_cast_result(node_t *operand, psx_type_t *cast_type,
                                         int type_size, int is_unsigned,
                                         int is_long_long);
node_t *psx_node_new_integer_cast_result_ex(node_t *operand, psx_type_t *cast_type,
                                            int type_size, int is_unsigned,
                                            int is_long_long, int is_plain_char,
                                            int widen_zext_i64);
node_t *psx_node_new_i64_to_i32_trunc_cast(node_t *operand, psx_type_t *cast_type,
                                           int is_unsigned);
node_t *psx_node_new_pointer_cast_result(node_t *operand, psx_type_t *cast_type,
                                         token_kind_t type_kind,
                                         token_kind_t tag_kind,
                                         char *tag_name, int tag_len,
                                         int elem_size, int is_unsigned);
node_t *psx_node_new_void_cast_result(node_t *operand, psx_type_t *cast_type);
node_t *psx_node_new_gvar_array_addr_for(struct global_var_t *gv);
node_t *psx_node_new_static_local_array_addr_for(struct lvar_t *var, int gvar_type_size);
node_t *psx_node_new_lvar_array_addr_for(struct lvar_t *var, int is_tag_pointer);
node_t *psx_node_new_compound_gvar_array_addr_for(struct global_var_t *gv,
                                                  int ptr_array_pointee_bytes,
                                                  int pointer_elem_size,
                                                  int array_size);
node_t *psx_node_new_compound_lvar_array_addr_for(struct lvar_t *var,
                                                  token_kind_t tag_kind,
                                                  char *tag_name, int tag_len,
                                                  int array_size);
node_t *psx_node_new_addr_value_for(node_t *operand);
node_t *psx_node_new_explicit_addr_value_for(node_t *operand);
node_t *psx_node_new_unary_addr_for(node_t *operand);
node_t *psx_node_new_tag_member_deref_for(node_t *addr_base, node_t *base,
                                          const struct tag_member_info_t *info);
node_t *psx_node_new_unary_deref_for(node_t *operand);
node_t *psx_node_new_subscript_deref_for(node_t *base, node_t *base_addr,
                                         node_t *scaled_offset,
                                         int elem_size, int inner_deref_size,
                                         int next_deref_size,
                                         const int *extra_strides,
                                         int extra_strides_count);
node_t *psx_node_new_byref_param_deref_for(struct lvar_t *var);
node_t *psx_node_new_member_lvar_ref_for(struct lvar_t *owner, int member_offset,
                                         int member_type_size, token_kind_t member_tag_kind,
                                         char *member_tag_name, int member_tag_len,
                                         int member_is_tag_pointer);
node_t *psx_node_new_tag_member_lvar_ref_for(struct lvar_t *owner, int member_offset,
                                             const struct tag_member_info_t *info);
node_t *psx_node_new_gvar_for(struct global_var_t *gv);
node_t *psx_node_new_gvar_array_base_for(struct global_var_t *gv);
node_t *psx_node_new_static_local_gvar_for(struct lvar_t *var, int type_size);
struct lvar_t *psx_node_lvar_symbol(node_t *node);
node_t *psx_node_clone_lvalue_with_lhs(node_t *target, node_t *lhs);
node_mem_t *psx_node_new_assign(node_t *lhs, node_t *rhs);

void psx_node_reject_const_assign(node_t *node, const char *op);
void psx_node_reject_const_qual_discard(node_t *lhs, node_t *rhs);
void psx_node_expect_lvalue(node_t *node, const char *op);
void psx_node_expect_incdec_target(node_t *node, const char *op);
node_t *psx_node_new_compound_assign(node_t *lhs, node_kind_t op_kind, node_t *rhs, const char *op);

#endif
