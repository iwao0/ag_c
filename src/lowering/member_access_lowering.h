#ifndef LOWERING_MEMBER_ACCESS_LOWERING_H
#define LOWERING_MEMBER_ACCESS_LOWERING_H

#include "../parser/ast.h"

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;

node_t *lower_member_access_expression_in(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    node_member_access_t *access,
    const token_t *fallback_diag_tok);

#endif
