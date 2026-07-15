#include "member_access_resolution.h"

#include "../parser/node_utils.h"

#include <string.h>

static int node_is_single_tag_array_view(node_t *node) {
  const psx_type_t *type = ps_node_get_type(node);
  return node &&
         (node->kind == ND_UNARY_DEREF || node->kind == ND_DEREF) && type &&
         type->kind == PSX_TYPE_ARRAY && type->base &&
         ps_type_is_tag_aggregate(type->base);
}

static int aggregate_member_index(
    const psx_record_decl_t *record, const tag_member_info_t *member,
    const char *member_name, int member_name_len) {
  if (!record || !member || !record->members) return -1;
  for (int i = 0; i < record->member_count; i++) {
    if (member == &record->members[i]) return i;
  }
  for (int i = 0; i < record->member_count; i++) {
    const tag_member_info_t *candidate = &record->members[i];
    if (candidate->name && candidate->len == member_name_len &&
        memcmp(candidate->name, member_name, (size_t)member_name_len) == 0)
      return i;
  }
  return -1;
}

static const tag_member_info_t *aggregate_member_named(
    const psx_record_decl_t *record, const char *member_name,
    int member_name_len) {
  if (!record || !record->members || !member_name || member_name_len <= 0)
    return NULL;
  for (int i = 0; i < record->member_count; i++) {
    const tag_member_info_t *member = &record->members[i];
    if (member->name && member->len == member_name_len &&
        memcmp(member->name, member_name, (size_t)member_name_len) == 0)
      return member;
  }
  return NULL;
}

void psx_resolve_member_access(
    const psx_member_access_resolution_request_t *request,
    psx_member_access_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_MEMBER_ACCESS_INVALID_BASE;
  resolution->member_index = -1;
  if (!request || !request->semantic_context || !request->base ||
      !request->member_name || request->member_name_len <= 0) {
    return;
  }

  const psx_type_t *base_type = ps_node_get_type(request->base);
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
  psx_semantic_context_t *semantic_context = request->semantic_context;
  resolution->record_id = ps_type_record_id(object_type);
  const psx_record_decl_t *record = ps_ctx_get_record_decl_in(
      semantic_context, resolution->record_id);

  const tag_member_info_t *member = aggregate_member_named(
      record, request->member_name, request->member_name_len);
  if (member) {
    resolution->member_index = aggregate_member_index(
        record, member,
        request->member_name, request->member_name_len);
    resolution->member = *member;
    resolution->status = PSX_MEMBER_ACCESS_OK;
    return;
  }

  int base_scope = object_type->tag_scope_depth_p1 > 0
                       ? object_type->tag_scope_depth_p1 - 1
                       : -1;
  int found = base_scope >= 0
      ? ps_ctx_find_tag_member_info_at_scope_in(
            semantic_context,
            object_type->tag_kind, object_type->tag_name,
            object_type->tag_len, base_scope,
            request->member_name, request->member_name_len,
            &resolution->member)
      : ps_ctx_find_tag_member_info_in(
            semantic_context,
            object_type->tag_kind, object_type->tag_name,
            object_type->tag_len,
            request->member_name, request->member_name_len,
            &resolution->member);
  resolution->status = found ? PSX_MEMBER_ACCESS_OK
                             : PSX_MEMBER_ACCESS_NOT_FOUND;
  if (found) {
    resolution->member_index = aggregate_member_index(
        record, &resolution->member,
        request->member_name, request->member_name_len);
  }
}
