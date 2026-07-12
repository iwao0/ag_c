#ifndef PARSER_NODE_FUNCPTR_PUBLIC_H
#define PARSER_NODE_FUNCPTR_PUBLIC_H

#include "core.h"
#include "node_fwd.h"

struct global_var_t;
struct lvar_t;
struct tag_member_info_t;

psx_decl_funcptr_sig_t ps_node_funcptr_sig(node_t *node);
psx_decl_funcptr_sig_t ps_node_funcdef_ret_funcptr_sig(const node_func_t *fn);
unsigned short ps_node_funcptr_param_fp_mask(node_t *node);
unsigned short ps_node_funcptr_param_int_mask(node_t *node);
int ps_node_has_funcptr_signature(node_t *node);
int ps_node_funcptr_returns_void(node_t *node);
int ps_node_funcptr_returns_complex(node_t *node);
int ps_node_funcptr_returns_pointee_array(node_t *node);
tk_float_kind_t ps_node_funcptr_ret_fp_kind(node_t *node);
#endif
