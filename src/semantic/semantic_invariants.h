#ifndef SEMANTIC_SEMANTIC_INVARIANTS_H
#define SEMANTIC_SEMANTIC_INVARIANTS_H

#include "../parser/ast.h"

typedef enum {
  PSX_SEMANTIC_INVARIANT_OK = 0,
  PSX_SEMANTIC_INVARIANT_RAW_EXPRESSION,
  PSX_SEMANTIC_INVARIANT_MISSING_CANONICAL_TYPE,
} psx_semantic_invariant_status_t;

typedef struct {
  psx_semantic_invariant_status_t status;
  const node_t *node;
} psx_semantic_invariant_failure_t;

int psx_semantic_tree_has_canonical_expression_types(
    const node_t *root, psx_semantic_invariant_failure_t *failure);

#endif
