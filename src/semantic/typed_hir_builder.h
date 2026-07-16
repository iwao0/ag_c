#ifndef SEMANTIC_TYPED_HIR_BUILDER_H
#define SEMANTIC_TYPED_HIR_BUILDER_H

#include "../hir/hir.h"
#include "../parser/node_fwd.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef enum {
  PSX_TYPED_HIR_BUILD_OK = 0,
  PSX_TYPED_HIR_BUILD_INVALID_INPUT,
  PSX_TYPED_HIR_BUILD_RAW_SYNTAX_REMAINS,
  PSX_TYPED_HIR_BUILD_MISSING_CANONICAL_TYPE,
  PSX_TYPED_HIR_BUILD_MISSING_RESOLVED_SYMBOL,
  PSX_TYPED_HIR_BUILD_OUT_OF_MEMORY,
} psx_typed_hir_build_status_t;

typedef struct {
  psx_typed_hir_build_status_t status;
  int source_node_kind;
} psx_typed_hir_build_failure_t;

int psx_build_typed_hir_root(
    psx_hir_module_t *module,
    const psx_semantic_context_t *semantic_context,
    const node_t *semantic_root,
    psx_typed_hir_build_failure_t *failure);

#endif
