#ifndef LOWERING_INITIALIZER_LOWERING_H
#define LOWERING_INITIALIZER_LOWERING_H

#include "../compilation_options.h"
#include "../parser/ast.h"

typedef struct psx_lowering_context_t psx_lowering_context_t;

node_t *lower_decl_initializer(
    psx_lowering_context_t *lowering_context,
    node_t *node, const ag_compilation_options_t *options);

#endif
