#ifndef PARSER_NODE_UTILS_H
#define PARSER_NODE_UTILS_H

#include "ast.h"

struct lvar_t;
struct global_var_t;

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
unsigned int psx_node_pointer_const_qual_mask(node_t *node);
unsigned int psx_node_pointer_volatile_qual_mask(node_t *node);
int psx_node_pointee_is_unsigned(node_t *node);
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
struct lvar_t *psx_node_lvar_symbol(node_t *node);
node_mem_t *psx_node_new_assign(node_t *lhs, node_t *rhs);

void psx_node_reject_const_assign(node_t *node, const char *op);
void psx_node_reject_const_qual_discard(node_t *lhs, node_t *rhs);
void psx_node_expect_lvalue(node_t *node, const char *op);
void psx_node_expect_incdec_target(node_t *node, const char *op);
node_t *psx_node_new_compound_assign(node_t *lhs, node_kind_t op_kind, node_t *rhs, const char *op);

#endif
