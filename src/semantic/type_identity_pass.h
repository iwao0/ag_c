#ifndef SEMANTIC_TYPE_IDENTITY_PASS_H
#define SEMANTIC_TYPE_IDENTITY_PASS_H

#include "../parser/node_fwd.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

int psx_intern_available_semantic_tree_types(
    psx_semantic_context_t *semantic_context, node_t *root,
    const node_t **failed_node);

#endif
