#ifndef SEMANTIC_MEMBER_ACCESS_RESOLUTION_H
#define SEMANTIC_MEMBER_ACCESS_RESOLUTION_H

#include "../parser/ast.h"
#include "resolution_state.h"
#include "resolved_node_type.h"
#include "../parser/semantic_ctx.h"

typedef enum {
  PSX_MEMBER_ACCESS_OK = 0,
  PSX_MEMBER_ACCESS_INVALID_BASE,
  PSX_MEMBER_ACCESS_NOT_FOUND,
} psx_member_access_status_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  node_t *base;
  char *member_name;
  int member_name_len;
  int from_pointer;
} psx_member_access_resolution_request_t;

typedef struct {
  psx_member_access_status_t status;
  psx_qual_type_t base_object_qual_type;
  const psx_type_t *base_object_type;
  psx_record_id_t record_id;
  int member_index;
  psx_record_member_decl_t declaration;
} psx_member_access_resolution_t;

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
