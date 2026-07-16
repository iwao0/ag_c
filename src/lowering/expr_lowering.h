#ifndef AGC_LOWERING_EXPR_LOWERING_H
#define AGC_LOWERING_EXPR_LOWERING_H

#include "../parser/ast.h"

typedef struct psx_lowering_context_t psx_lowering_context_t;

node_t *lower_additive_expression(
    psx_lowering_context_t *lowering_context,
    psx_syntax_node_kind_t kind, node_t *lhs, node_t *rhs);

#endif
