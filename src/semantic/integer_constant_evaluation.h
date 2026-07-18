#ifndef SEMANTIC_INTEGER_CONSTANT_EVALUATION_H
#define SEMANTIC_INTEGER_CONSTANT_EVALUATION_H

#include "../parser/syntax_node_kind.h"

typedef struct psx_type_t psx_type_t;

int psx_normalize_integer_constant_cast(
    const psx_type_t *target, long long operand, long long *result);
int psx_apply_integer_constant_binary(
    psx_syntax_node_kind_t operation,
    long long lhs, long long rhs, long long *result);

#endif
