#ifndef SEMANTIC_HIR_MEMBER_RESOLUTION_H
#define SEMANTIC_HIR_MEMBER_RESOLUTION_H

#include "../hir/hir_internal.h"
#include "member_resolution.h"

typedef struct {
  psx_member_access_resolution_t member;
  psx_hir_node_spec_t node_spec;
} psx_hir_member_resolution_t;

int psx_resolve_member_hir_node_spec_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t base_qual_type,
    const char *member_name,
    int member_name_len,
    int from_pointer,
    psx_hir_member_resolution_t *resolution);

#endif
