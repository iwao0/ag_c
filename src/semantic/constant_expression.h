#ifndef SEMANTIC_CONSTANT_EXPRESSION_H
#define SEMANTIC_CONSTANT_EXPRESSION_H

#include "../parser/ast.h"

long long psx_eval_const_int(node_t *node, int *ok);
double psx_eval_const_fp(node_t *node, int *ok);
int psx_resolve_static_address_constant(
    node_t *node, char **symbol, int *symbol_len, long long *offset);

#endif
