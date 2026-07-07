#ifndef PARSER_NODE_PUBLIC_H
#define PARSER_NODE_PUBLIC_H

#include "ast.h"

struct global_var_t;
struct lvar_t;

int ps_node_is_pointer(node_t *n);
int ps_node_deref_size(node_t *n);
int ps_node_type_size(node_t *n);
int psx_node_storage_type_size(node_t *n);
int psx_node_integer_promotion_is_unsigned(node_t *n);
int psx_node_conversion_value_is_unsigned(node_t *n);
int psx_node_i64_widen_source_is_unsigned(node_t *n);
int psx_node_shift_operation_is_unsigned(node_t *n);
int psx_node_usual_arith_operands_is_unsigned(node_t *lhs, node_t *rhs);
int psx_node_usual_arith_is_unsigned(node_t *n);
int psx_node_atomic_pointer_info(node_t *ptr_arg, int *width, int *is_unsigned);
int psx_node_cast_i64_extension_info(node_t *node, int *target_size,
                                     int *widen_zext_i64, int *needs_i64_extend);
int psx_node_pointer_qual_levels(node_t *n);
int psx_node_bitfield_info(node_t *node, int *bit_width, int *bit_offset,
                           int *bit_is_signed);
int psx_node_value_is_pointer_like(node_t *node);
int psx_node_aggregate_value_size(node_t *node);
int psx_node_is_unsigned_type(node_t *node);
int psx_node_deref_decays_to_address(node_t *node);
void psx_node_get_tag_type(node_t *node, token_kind_t *tag_kind,
                           char **tag_name, int *tag_len, int *is_tag_pointer);
int psx_node_mem_has_funcptr_metadata(const node_mem_t *mem);
psx_decl_funcptr_sig_t psx_node_mem_funcptr_sig(const node_mem_t *mem);
psx_decl_funcptr_sig_t psx_node_funcptr_sig(node_t *node);
void psx_node_store_funcptr_metadata(node_mem_t *dst, psx_decl_funcptr_sig_t sig);
psx_decl_funcptr_sig_t psx_node_funcdef_ret_funcptr_sig(const node_func_t *fn);
void psx_node_funcdef_set_ret_funcptr_sig(node_func_t *fn, psx_decl_funcptr_sig_t sig);
void psx_node_copy_funcptr_metadata(node_mem_t *dst, node_t *src);
void psx_node_copy_funcptr_metadata_from_lvar(node_mem_t *dst, const struct lvar_t *src);
void psx_node_copy_funcptr_metadata_from_gvar(node_mem_t *dst, const struct global_var_t *src);
void psx_node_merge_funcptr_metadata_from_lvar(node_mem_t *dst, const struct lvar_t *src);
void psx_node_merge_funcptr_metadata_from_gvar(node_mem_t *dst, const struct global_var_t *src);
int psx_node_vla_alloc_descriptor_info(node_t *node, int *descriptor_frame_off,
                                       int *row_stride_frame_off);
int psx_node_vla_row_stride_frame_off(node_t *n);

#endif
