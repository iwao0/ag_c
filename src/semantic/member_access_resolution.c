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
  resolution->base_tag_kind = TK_EOF;
  if (!request || !request->base || !request->member_name ||
      request->member_name_len <= 0) {
    return;
  }

  ps_node_get_tag_type(
      request->base, &resolution->base_tag_kind,
      &resolution->base_tag_name, &resolution->base_tag_name_len,
      &resolution->base_is_pointer);
  psx_type_t *base_type = ps_node_get_type(request->base);
  const psx_type_t *object_type = base_type;
  if (resolution->base_is_pointer && object_type && object_type->base)
    object_type = object_type->base;
  if (object_type && ps_type_is_tag_aggregate(object_type))
    resolution->base_object_size = ps_type_sizeof(object_type);
  if (resolution->base_object_size <= 0 &&
      resolution->base_tag_kind != TK_EOF) {
    resolution->base_object_size = ps_ctx_get_tag_size(
        resolution->base_tag_kind, resolution->base_tag_name,
        resolution->base_tag_name_len);
  }
  if (!request->from_pointer && resolution->base_is_pointer &&
      (node_is_single_tag_array_view(request->base) ||
       request->base->kind == ND_UNARY_DEREF ||
       request->base->kind == ND_DEREF)) {
    resolution->base_is_pointer = 0;
  }
  if (resolution->base_tag_kind == TK_EOF ||
      (!request->from_pointer && resolution->base_is_pointer) ||
      (request->from_pointer && !resolution->base_is_pointer)) {
    return;
  }

  const tag_member_info_t *member = ps_type_find_aggregate_member(
      ps_node_get_type(request->base), resolution->base_tag_kind,
      resolution->base_tag_name, resolution->base_tag_name_len,
      request->member_name, request->member_name_len);
  if (member) {
    resolution->member = *member;
    resolution->status = PSX_MEMBER_ACCESS_OK;
    return;
  }

  int base_scope = ps_node_get_tag_scope_depth(request->base);
  int found = base_scope >= 0
      ? ps_ctx_find_tag_member_info_at_scope(
            resolution->base_tag_kind, resolution->base_tag_name,
            resolution->base_tag_name_len, base_scope,
            request->member_name, request->member_name_len,
            &resolution->member)
      : ps_ctx_find_tag_member_info(
            resolution->base_tag_kind, resolution->base_tag_name,
            resolution->base_tag_name_len,
            request->member_name, request->member_name_len,
            &resolution->member);
  resolution->status = found ? PSX_MEMBER_ACCESS_OK
                             : PSX_MEMBER_ACCESS_NOT_FOUND;
}
