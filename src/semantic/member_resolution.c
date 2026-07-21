#include "member_resolution.h"

#include <string.h>

void psx_resolve_member_access_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t base_qual_type,
    const char *member_name,
    int member_name_len,
    int from_pointer,
    psx_member_access_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_MEMBER_ACCESS_INVALID_BASE;
  resolution->member_index = -1;
  resolution->member_qual_type = (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!semantic_context ||
      base_qual_type.type_id == PSX_TYPE_ID_INVALID ||
      !member_name || member_name_len <= 0)
    return;

  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t base_shape = {0};
  if (!psx_semantic_type_table_describe(
          semantic_types, base_qual_type.type_id, &base_shape))
    return;
  psx_qual_type_t object_qual_type = base_qual_type;
  if (from_pointer) {
    if (base_shape.kind != PSX_TYPE_POINTER &&
        base_shape.kind != PSX_TYPE_ARRAY)
      return;
    object_qual_type = psx_semantic_type_table_base(
        semantic_types, base_qual_type.type_id);
  } else if (base_shape.kind != PSX_TYPE_STRUCT &&
             base_shape.kind != PSX_TYPE_UNION) {
    return;
  }
  psx_type_shape_t object_shape = {0};
  if (!psx_semantic_type_table_describe(
          semantic_types, object_qual_type.type_id, &object_shape) ||
      (object_shape.kind != PSX_TYPE_STRUCT &&
       object_shape.kind != PSX_TYPE_UNION))
    return;

  resolution->base_object_qual_type = object_qual_type;
  resolution->record_id = object_shape.record_id;
  if (!ps_ctx_find_record_member_in(
          semantic_context, resolution->record_id,
          member_name, member_name_len, &resolution->member_index,
          &resolution->declaration)) {
    resolution->status = PSX_MEMBER_ACCESS_NOT_FOUND;
    return;
  }

  resolution->member_qual_type =
      psx_semantic_type_table_record_member(
          semantic_types, object_qual_type.type_id,
          resolution->member_index);
  if (resolution->member_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    resolution->status = PSX_MEMBER_ACCESS_NOT_FOUND;
    return;
  }
  resolution->member_qual_type.qualifiers |=
      object_qual_type.qualifiers;

  resolution->status = PSX_MEMBER_ACCESS_OK;
}
