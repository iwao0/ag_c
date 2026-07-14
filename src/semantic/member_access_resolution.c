#include "member_access_resolution.h"

#include "../parser/node_utils.h"

#include <string.h>

static int node_is_single_tag_array_view(node_t *node) {
  psx_type_t *type = ps_node_get_type(node);
  return node &&
         (node->kind == ND_UNARY_DEREF || node->kind == ND_DEREF) && type &&
         type->kind == PSX_TYPE_ARRAY && type->base &&
         ps_type_is_tag_aggregate(type->base);
}

void psx_resolve_member_access(
    const psx_member_access_resolution_request_t *request,
    psx_member_access_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_MEMBER_ACCESS_INVALID_BASE;
  if (!request || !request->base || !request->member_name ||
      request->member_name_len <= 0) {
    return;
  }

  psx_type_t *base_type = ps_node_get_type(request->base);
  const psx_type_t *object_type =
      ps_type_find_aggregate_object_type(base_type);
  int base_is_pointer = base_type &&
                        (base_type->kind == PSX_TYPE_POINTER ||
                         base_type->kind == PSX_TYPE_ARRAY);
  if (!request->from_pointer && base_is_pointer &&
      (node_is_single_tag_array_view(request->base) ||
       request->base->kind == ND_UNARY_DEREF ||
       request->base->kind == ND_DEREF)) {
    base_is_pointer = 0;
  }
  if (!object_type ||
      (!request->from_pointer && base_is_pointer) ||
      (request->from_pointer && !base_is_pointer)) {
    return;
  }
  resolution->base_object_type = object_type;

  const tag_member_info_t *member = ps_type_find_aggregate_member(
      base_type, object_type->tag_kind,
      object_type->tag_name, object_type->tag_len,
      request->member_name, request->member_name_len);
  if (member) {
    resolution->member = *member;
    resolution->status = PSX_MEMBER_ACCESS_OK;
    return;
  }

  int base_scope = object_type->tag_scope_depth_p1 > 0
                       ? object_type->tag_scope_depth_p1 - 1
                       : -1;
  int found = base_scope >= 0
      ? ps_ctx_find_tag_member_info_at_scope(
            object_type->tag_kind, object_type->tag_name,
            object_type->tag_len, base_scope,
            request->member_name, request->member_name_len,
            &resolution->member)
      : ps_ctx_find_tag_member_info(
            object_type->tag_kind, object_type->tag_name,
            object_type->tag_len,
            request->member_name, request->member_name_len,
            &resolution->member);
  resolution->status = found ? PSX_MEMBER_ACCESS_OK
                             : PSX_MEMBER_ACCESS_NOT_FOUND;
}
