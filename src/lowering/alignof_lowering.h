#ifndef PSX_ALIGNOF_LOWERING_H
#define PSX_ALIGNOF_LOWERING_H

#include "../parser/ast.h"

node_t *lower_alignof_query_expression(node_alignof_query_t *query);

#endif
