#ifndef LOWERING_INITIALIZER_LOWERING_H
#define LOWERING_INITIALIZER_LOWERING_H

#include "../parser/ast.h"

node_t *lower_decl_initializer(node_t *node);
int psx_initializer_lowering_supports_recursive_aggregate(
    const psx_type_t *type);

#endif
