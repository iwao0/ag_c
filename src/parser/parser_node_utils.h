#ifndef PARSER_NODE_UTILS_H
#define PARSER_NODE_UTILS_H

#include "ast.h"

int pnode_type_size(node_t *node);
int pnode_deref_size(node_t *node);

node_t *pnode_new_binary(node_kind_t kind, node_t *lhs, node_t *rhs);
node_t *pnode_new_num(long long val);
node_t *pnode_new_lvar(int offset);
node_t *pnode_new_lvar_typed(int offset, int type_size);
node_mem_t *pnode_new_assign(node_t *lhs, node_t *rhs);

void pnode_expect_lvalue(node_t *node, const char *op);
void pnode_expect_incdec_target(node_t *node, const char *op);
node_t *pnode_new_compound_assign(node_t *lhs, node_kind_t op_kind, node_t *rhs, const char *op);

#endif

