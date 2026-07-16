#include "typed_hir_materialization.h"

#include <string.h>

#include "semantic_node_internal.h"
#include "semantic_tree_internal.h"

psx_typed_hir_tree_t *psx_materialize_typed_hir_tree(
    const psx_semantic_tree_t *semantic_tree,
    psx_resolved_hir_build_failure_t *failure) {
  if (failure) memset(failure, 0, sizeof(*failure));
  const psx_semantic_node_t *root =
      psx_semantic_tree_root(semantic_tree);
  if (!semantic_tree || !root) {
    if (failure) {
      failure->status = root
                            ? PSX_RESOLVED_HIR_BUILD_INVALID_INPUT
                            : PSX_RESOLVED_HIR_BUILD_UNMATERIALIZED;
      failure->source_node_kind =
          root ? root->source_node_kind : -1;
    }
    return NULL;
  }
  return psx_semantic_tree_typed_hir_view(semantic_tree);
}
