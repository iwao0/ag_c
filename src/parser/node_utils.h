#ifndef PARSER_NODE_UTILS_H
#define PARSER_NODE_UTILS_H

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
void psx_node_copy_funcptr_metadata(node_mem_t *dst, node_t *src);
void psx_node_copy_funcptr_metadata_from_lvar(node_mem_t *dst, const struct lvar_t *src);
void psx_node_copy_funcptr_metadata_from_gvar(node_mem_t *dst, const struct global_var_t *src);
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
node_t *psx_node_new_lvar_for(struct lvar_t *var);
node_t *psx_node_new_lvar_typed_for(struct lvar_t *var, int type_size);
node_t *psx_node_new_lvar_expr_ref_for(struct lvar_t *var, int is_pointer);
node_t *psx_node_new_lvar_identifier_ref_for(struct lvar_t *var);
node_t *psx_node_new_member_lvar_ref_for(struct lvar_t *owner, int member_offset,
                                         int member_type_size, token_kind_t member_tag_kind,
                                         char *member_tag_name, int member_tag_len,
                                         int member_is_tag_pointer);
node_t *psx_node_new_gvar_for(struct global_var_t *gv);
node_t *psx_node_new_gvar_array_base_for(struct global_var_t *gv);
node_t *psx_node_new_static_local_gvar_for(struct lvar_t *var, int type_size);
struct lvar_t *psx_node_lvar_symbol(node_t *node);
node_mem_t *psx_node_new_assign(node_t *lhs, node_t *rhs);

void psx_node_reject_const_assign(node_t *node, const char *op);
void psx_node_reject_const_qual_discard(node_t *lhs, node_t *rhs);
void psx_node_expect_lvalue(node_t *node, const char *op);
void psx_node_expect_incdec_target(node_t *node, const char *op);
node_t *psx_node_new_compound_assign(node_t *lhs, node_kind_t op_kind, node_t *rhs, const char *op);

#endif
