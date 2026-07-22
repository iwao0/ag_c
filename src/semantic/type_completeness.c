#include "type_completeness.h"

#include "../parser/semantic_ctx.h"
#include "record_decl.h"
#include "type_identity.h"

int psx_semantic_type_is_complete_object_in(
    psx_semantic_context_t *semantic_context,
    psx_type_id_t type_id) {
  if (!semantic_context || type_id == PSX_TYPE_ID_INVALID)
    return 0;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t type = {0};
  if (!psx_semantic_type_table_describe(types, type_id, &type) ||
      type.kind == PSX_TYPE_INVALID || type.kind == PSX_TYPE_VOID ||
      type.kind == PSX_TYPE_FUNCTION)
    return 0;
  if (psx_type_kind_is_aggregate(type.kind)) {
    const psx_record_decl_t *record = ps_ctx_get_record_decl_in(
        semantic_context, type.record_id);
    return record && record->is_complete;
  }
  if (type.kind == PSX_TYPE_ARRAY) {
    psx_qual_type_t element =
        psx_semantic_type_table_base(types, type_id);
    return (type.array_len > 0 || type.is_vla) &&
           psx_semantic_type_is_complete_object_in(
               semantic_context, element.type_id);
  }
  return 1;
}

int psx_semantic_pointer_points_to_complete_object_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t pointer_type) {
  if (!semantic_context ||
      pointer_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t pointer = {0};
  if (!psx_semantic_type_table_describe(
          types, pointer_type.type_id, &pointer) ||
      pointer.kind != PSX_TYPE_POINTER)
    return 0;
  psx_qual_type_t pointee =
      psx_semantic_type_table_base(types, pointer_type.type_id);
  return psx_semantic_type_is_complete_object_in(
      semantic_context, pointee.type_id);
}
