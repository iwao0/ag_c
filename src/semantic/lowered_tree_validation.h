#ifndef SEMANTIC_LOWERED_TREE_VALIDATION_H
#define SEMANTIC_LOWERED_TREE_VALIDATION_H

#include "../parser/node_fwd.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct token_t token_t;

void psx_validate_lowered_tree_in_context(
    psx_semantic_context_t *semantic_context, const node_t *root,
    const token_t *fallback_diag_tok);

#endif
