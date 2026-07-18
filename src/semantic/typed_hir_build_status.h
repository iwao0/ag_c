#ifndef SEMANTIC_TYPED_HIR_BUILD_STATUS_H
#define SEMANTIC_TYPED_HIR_BUILD_STATUS_H

typedef struct token_t token_t;

typedef enum {
  PSX_RESOLVED_HIR_BUILD_OK = 0,
  PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
  PSX_RESOLVED_HIR_BUILD_UNFINALIZED_RESOLUTION,
  PSX_RESOLVED_HIR_BUILD_UNMATERIALIZED,
  PSX_RESOLVED_HIR_BUILD_RAW_SYNTAX_REMAINS,
  PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE,
  PSX_RESOLVED_HIR_BUILD_MISSING_RESOLVED_SYMBOL,
  PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
} psx_resolved_hir_build_status_t;

typedef struct psx_resolved_hir_build_failure_t {
  psx_resolved_hir_build_status_t status;
  int source_node_kind;
  const token_t *source_token;
} psx_resolved_hir_build_failure_t;

#endif
