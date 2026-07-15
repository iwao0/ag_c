#include "type_identity_pass.h"

#include "tree_walk.h"
#include "../parser/semantic_ctx.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  const node_t *failed_node;
} type_identity_pass_t;

static int intern_available_type(const node_t *node, void *user) {
  type_identity_pass_t *pass = user;
  if (!node->type) return 1;
  psx_qual_type_t type =
      ps_ctx_intern_qual_type_in(pass->semantic_context, node->type);
  if (type.type_id != PSX_TYPE_ID_INVALID) return 1;
  pass->failed_node = node;
  return 0;
}

int psx_intern_available_semantic_tree_types(
    psx_semantic_context_t *semantic_context, const node_t *root,
    const node_t **failed_node) {
  if (failed_node) *failed_node = NULL;
  if (!semantic_context) {
    if (failed_node) *failed_node = root;
    return 0;
  }
  type_identity_pass_t pass = {
      .semantic_context = semantic_context,
  };
  int ok = psx_walk_semantic_tree(root, intern_available_type, &pass);
  if (!ok && failed_node) *failed_node = pass.failed_node;
  return ok;
}
