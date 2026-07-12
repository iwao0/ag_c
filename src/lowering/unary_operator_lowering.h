#ifndef PSX_UNARY_OPERATOR_LOWERING_H
#define PSX_UNARY_OPERATOR_LOWERING_H

#include "../parser/ast.h"

node_t *lower_unary_negate_expression(node_t *node);
node_t *lower_complex_part_expression(node_t *node);

#endif
