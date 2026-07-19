#include "member_access_resolution.h"
#include "resolved_node_kind.h"

#include "../parser/node_utils.h"

#include <string.h>

static int node_is_single_tag_array_view(
    const psx_resolution_store_t *store, node_t *node) {
  const psx_type_t *type = ps_node_get_type(store, node);
  return node &&
         (node->kind == ND_UNARY_DEREF ||
          psx_resolution_node_kind(store, node) == ND_DEREF) && type &&
         type->kind == PSX_TYPE_ARRAY && type->base &&
         ps_type_is_tag_aggregate(type->base);
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

  psx_semantic_context_t *semantic_context = request->semantic_context;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(semantic_context);
  const psx_type_t *base_type = ps_node_get_type(store, request->base);
  psx_qual_type_t base_qual_type = ps_node_qual_type(store, request->base);
  if (base_qual_type.type_id == PSX_TYPE_ID_INVALID ||
      base_type != psx_semantic_type_table_lookup(
                       semantic_types, base_qual_type.type_id)) {
    base_qual_type = ps_ctx_intern_qual_type_in(
        semantic_context, base_type);
  }
  if (base_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    resolution->status = PSX_MEMBER_ACCESS_NOT_FOUND;
    return;
  }
  if (!request->from_pointer &&
      node_is_single_tag_array_view(store, request->base)) {
    base_qual_type = psx_semantic_type_table_base(
        semantic_types, base_qual_type.type_id);
  }
  psx_resolve_member_access_qual_type_in(
      semantic_context, base_qual_type,
      request->member_name, request->member_name_len,
      request->from_pointer, resolution);
}
