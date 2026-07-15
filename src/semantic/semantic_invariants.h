#ifndef SEMANTIC_SEMANTIC_INVARIANTS_H
#define SEMANTIC_SEMANTIC_INVARIANTS_H

#include "../parser/ast.h"

typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef enum {
  PSX_SEMANTIC_INVARIANT_OK = 0,
  PSX_SEMANTIC_INVARIANT_RAW_EXPRESSION,
  PSX_SEMANTIC_INVARIANT_INTERMEDIATE_INITIALIZER_SYNTAX,
  PSX_SEMANTIC_INVARIANT_MISSING_CANONICAL_TYPE,
  PSX_SEMANTIC_INVARIANT_INVALID_CANONICAL_TYPE,
  PSX_SEMANTIC_INVARIANT_UNINTERNED_CANONICAL_TYPE,
  PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE,
  PSX_SEMANTIC_INVARIANT_INVALID_VLA_RUNTIME_VIEW,
  PSX_SEMANTIC_INVARIANT_INVALID_NODE_KIND,
} psx_semantic_invariant_status_t;

typedef struct {
  psx_semantic_invariant_status_t status;
  const node_t *node;
} psx_semantic_invariant_failure_t;

int psx_semantic_tree_has_canonical_expression_types(
    const node_t *root, psx_semantic_invariant_failure_t *failure);
int psx_finalize_semantic_tree_type_identities(
    psx_semantic_context_t *semantic_context, const node_t *root,
    psx_semantic_invariant_failure_t *failure,
    int allow_initializer_syntax);
void psx_require_semantic_tree_has_interned_expression_types(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, const node_t *root,
    const token_t *fallback_diag_tok);
void psx_require_semantic_initializer_has_interned_expression_types(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, const node_t *root,
    const token_t *fallback_diag_tok);
void psx_require_semantic_tree_has_canonical_expression_types(
    ag_diagnostic_context_t *diagnostics, const node_t *root,
    const token_t *fallback_diag_tok);
void psx_require_semantic_initializer_has_canonical_expression_types(
    ag_diagnostic_context_t *diagnostics, const node_t *root,
    const token_t *fallback_diag_tok);

#endif
