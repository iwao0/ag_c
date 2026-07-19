#ifndef SEMANTIC_TYPED_HIR_DIAGNOSTICS_H
#define SEMANTIC_TYPED_HIR_DIAGNOSTICS_H

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_typed_hir_tree_t psx_typed_hir_tree_t;
typedef struct token_t token_t;

void psx_emit_typed_hir_warnings(
    psx_semantic_context_t *semantic_context,
    const psx_typed_hir_tree_t *tree,
    const token_t *fallback_diag_tok);

#endif
