#ifndef PSX_UNARY_OPERATOR_LOWERING_H
#define PSX_UNARY_OPERATOR_LOWERING_H

#include "../parser/ast.h"

typedef struct psx_lowering_context_t psx_lowering_context_t;

node_t *lower_unary_negate_expression(
    psx_lowering_context_t *lowering_context, node_t *node);
node_t *lower_complex_part_expression(
    psx_lowering_context_t *lowering_context, node_t *node);

#endif
