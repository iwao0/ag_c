#include "lowered_tree_validation.h"

#include "../parser/semantic_ctx.h"
#include "assignment_validation.h"
#include "tree_walk.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  ag_diagnostic_context_t *diagnostics;
  const token_t *fallback_diag_tok;
} lowered_tree_validation_t;

static int validate_lowered_node(
    const node_t *node, void *user) {
  lowered_tree_validation_t *validation = user;
  psx_validate_assignment_in_context(
      validation->semantic_context, node,
      validation->diagnostics,
      validation->fallback_diag_tok);
  return 1;
}

void psx_validate_lowered_tree_in_context(
    psx_semantic_context_t *semantic_context, const node_t *root,
    const token_t *fallback_diag_tok) {
  if (!semantic_context || !root) return;
  lowered_tree_validation_t validation = {
      .semantic_context = semantic_context,
      .diagnostics = ps_ctx_diagnostics(semantic_context),
      .fallback_diag_tok = fallback_diag_tok,
  };
  psx_walk_semantic_tree(
      ps_ctx_resolution_store(semantic_context), root,
      validate_lowered_node, &validation);
}
