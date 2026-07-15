#ifndef PSX_SUBSCRIPT_LOWERING_H
#define PSX_SUBSCRIPT_LOWERING_H

#include "../parser/ast.h"

typedef struct psx_lowering_context_t psx_lowering_context_t;

node_t *lower_subscript_expression(
    psx_lowering_context_t *lowering_context, node_t *node);

#endif
