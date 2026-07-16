#ifndef SEMANTIC_RESOLUTION_WORK_TREE_H
#define SEMANTIC_RESOLUTION_WORK_TREE_H

#include "../parser/node_fwd.h"

typedef struct arena_context_t arena_context_t;

node_t *psx_clone_syntax_tree_for_resolution(
    arena_context_t *arena_context, const node_t *syntax_root);

#endif
