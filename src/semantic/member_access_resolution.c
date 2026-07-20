#include "member_access_resolution.h"
#include "resolved_node_kind.h"

#include <string.h>

static int node_is_single_record_array(
    const psx_semantic_type_table_t *types,
    const psx_resolution_store_t *store, const node_t *node,
    psx_qual_type_t type) {
  psx_type_shape_t array_shape = {0};
  if (!types || !node || type.type_id == PSX_TYPE_ID_INVALID ||
      (node->kind != ND_UNARY_DEREF &&
       psx_resolution_node_kind(store, node) != ND_DEREF) ||
      !psx_semantic_type_table_describe(
          types, type.type_id, &array_shape) ||
      array_shape.kind != PSX_TYPE_ARRAY)
    return 0;
  psx_qual_type_t element = psx_semantic_type_table_base(
      types, type.type_id);
  psx_type_shape_t element_shape = {0};
  return psx_semantic_type_table_describe(
             types, element.type_id, &element_shape) &&
         (element_shape.kind == PSX_TYPE_STRUCT ||
          element_shape.kind == PSX_TYPE_UNION);
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
  psx_qual_type_t base_qual_type = ps_node_qual_type(store, request->base);
  if (base_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    resolution->status = PSX_MEMBER_ACCESS_NOT_FOUND;
    return;
  }
  if (!request->from_pointer &&
      node_is_single_record_array(
          semantic_types, store, request->base, base_qual_type)) {
    base_qual_type = psx_semantic_type_table_base(
        semantic_types, base_qual_type.type_id);
  }
  psx_resolve_member_access_qual_type_in(
      semantic_context, base_qual_type,
      request->member_name, request->member_name_len,
      request->from_pointer, resolution);
}
