#ifndef AGC_LOWERING_EXPR_LOWERING_H
#define AGC_LOWERING_EXPR_LOWERING_H

#include "../parser/ast.h"

node_t *lower_additive_expression(node_kind_t kind, node_t *lhs, node_t *rhs);

#endif
