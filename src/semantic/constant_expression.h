#ifndef SEMANTIC_CONSTANT_EXPRESSION_H
#define SEMANTIC_CONSTANT_EXPRESSION_H

#include "../parser/ast.h"

typedef struct psx_resolution_store_t psx_resolution_store_t;

long long psx_eval_const_int(
    const psx_resolution_store_t *store, node_t *node, int *ok);
double psx_eval_const_fp(
    const psx_resolution_store_t *store, node_t *node, int *ok);
int psx_resolve_static_address_constant(
    const psx_resolution_store_t *store, node_t *node,
    char **symbol, int *symbol_len, long long *offset);

#endif
