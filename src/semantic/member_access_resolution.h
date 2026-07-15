#ifndef SEMANTIC_MEMBER_ACCESS_RESOLUTION_H
#define SEMANTIC_MEMBER_ACCESS_RESOLUTION_H

#include "../parser/ast.h"
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
  const psx_type_t *base_object_type;
  int member_index;
  tag_member_info_t member;
} psx_member_access_resolution_t;

void psx_resolve_member_access(
    const psx_member_access_resolution_request_t *request,
    psx_member_access_resolution_t *resolution);

#endif
