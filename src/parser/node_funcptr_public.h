#ifndef PARSER_NODE_FUNCPTR_PUBLIC_H
#define PARSER_NODE_FUNCPTR_PUBLIC_H

#include "core.h"
#include "node_fwd.h"

struct global_var_t;
struct lvar_t;
struct tag_member_info_t;

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
unsigned short psx_node_funcptr_param_fp_mask(node_t *node);
unsigned short psx_node_funcptr_param_int_mask(node_t *node);
int psx_node_has_funcptr_signature(node_t *node);
int psx_node_funcptr_returns_void(node_t *node);
int psx_node_funcptr_returns_complex(node_t *node);
int psx_node_funcptr_returns_pointee_array(node_t *node);
tk_float_kind_t psx_node_funcptr_ret_fp_kind(node_t *node);
void psx_node_copy_funcptr_metadata_from_tag_member(node_mem_t *dst,
                                                    const struct tag_member_info_t *src);

#endif
