#ifndef PARSER_NODE_FUNCPTR_PUBLIC_H
#define PARSER_NODE_FUNCPTR_PUBLIC_H

#include "core.h"
#include "node_fwd.h"

struct global_var_t;
struct lvar_t;
struct tag_member_info_t;

psx_decl_funcptr_sig_t ps_node_funcptr_sig(node_t *node);
void ps_node_get_funcptr_sig(node_t *node, psx_decl_funcptr_sig_t *out);
psx_decl_funcptr_sig_t ps_node_funcdef_ret_funcptr_sig(const node_func_t *fn);
#endif
