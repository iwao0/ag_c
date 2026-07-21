#include "typed_hir_build_status.h"

void psx_resolved_hir_build_failure_init(
    psx_resolved_hir_build_failure_t *failure) {
  if (!failure) return;
  *failure = (psx_resolved_hir_build_failure_t){
      .source_node_kind = -1,
  };
}

void psx_resolved_hir_build_failure_note(
    psx_resolved_hir_build_failure_t *failure,
    psx_resolved_hir_build_status_t status,
    int source_node_kind, const token_t *source_token) {
  if (!failure) return;
  if (failure->status == PSX_RESOLVED_HIR_BUILD_OK)
    failure->status = status;
  if (failure->source_node_kind < 0)
    failure->source_node_kind = source_node_kind;
  if (!failure->source_token)
    failure->source_token = source_token;
}
