#ifndef PSX_ALIGNOF_LOWERING_H
#define PSX_ALIGNOF_LOWERING_H

#include "../parser/ast.h"

typedef struct psx_lowering_context_t psx_lowering_context_t;

node_t *lower_alignof_query_expression(
    psx_lowering_context_t *lowering_context,
    node_alignof_query_t *query);

#endif
