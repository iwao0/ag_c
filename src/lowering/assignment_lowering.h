#ifndef AGC_LOWERING_ASSIGNMENT_LOWERING_H
#define AGC_LOWERING_ASSIGNMENT_LOWERING_H

#include "../parser/ast.h"

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;

node_t *lower_compound_assignment_expression(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry, node_t *node);

#endif
