#ifndef LOWERING_INITIALIZER_LOWERING_H
#define LOWERING_INITIALIZER_LOWERING_H

#include "../compilation_options.h"
#include "../parser/ast.h"

node_t *lower_decl_initializer(
    node_t *node, const ag_compilation_options_t *options);
int psx_initializer_lowering_supports_recursive_aggregate(
    const psx_type_t *type);

#endif
