#ifndef SEMANTIC_MEMBER_ACCESS_RESOLUTION_H
#define SEMANTIC_MEMBER_ACCESS_RESOLUTION_H

#include "../parser/ast.h"
#include "member_resolution.h"
#include "resolution_state.h"
#include "resolved_node_type.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  node_t *base;
  char *member_name;
  int member_name_len;
  int from_pointer;
} psx_member_access_resolution_request_t;

void psx_resolve_member_access(
    const psx_member_access_resolution_request_t *request,
    psx_member_access_resolution_t *resolution);

static inline const psx_member_access_state_t *
psx_member_access_state(const node_member_access_t *access) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(access ? &access->base : NULL);
  return state ? &state->member_access : NULL;
}

static inline psx_member_access_state_t *
psx_member_access_state_mut(node_member_access_t *access) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(access ? &access->base : NULL);
  return state ? &state->member_access : NULL;
}

#endif
