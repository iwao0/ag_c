#ifndef PSX_SIZEOF_LOWERING_H
#define PSX_SIZEOF_LOWERING_H

#include "../parser/ast.h"

node_t *lower_sizeof_query_expression(
    node_sizeof_query_t *query, node_t *evaluated_prefix);

#endif
