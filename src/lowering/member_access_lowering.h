#ifndef LOWERING_MEMBER_ACCESS_LOWERING_H
#define LOWERING_MEMBER_ACCESS_LOWERING_H

#include "../parser/ast.h"

node_t *lower_member_access_expression(
    node_member_access_t *access,
    const token_t *fallback_diag_tok);

#endif
