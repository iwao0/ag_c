#include "aggregate_cast_resolution.h"

#include <string.h>

#include "../type_layout.h"
#include "record_decl_table.h"
#include "record_layout.h"
#include "type_identity.h"

void psx_resolve_aggregate_cast_qual_types(
    const psx_semantic_type_table_t *types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_data_layout_t *data_layout, psx_qual_type_t target_qual_type,
    psx_qual_type_t operand_qual_type, const ag_compilation_options_t *options,
    psx_aggregate_cast_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_AGGREGATE_CAST_STATUS_INVALID;
  resolution->target_qual_type = target_qual_type;
  resolution->member_qual_type = (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  resolution->member_index = -1;
  if (!types || !record_decls || !options ||
      target_qual_type.type_id == PSX_TYPE_ID_INVALID ||
      operand_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return;

  psx_type_shape_t target_type = {0};
  psx_type_shape_t operand_type = {0};
  if (!psx_semantic_type_table_describe(
          types, target_qual_type.type_id, &target_type) ||
      !psx_semantic_type_table_describe(
          types, operand_qual_type.type_id, &operand_type) ||
      !psx_type_kind_is_aggregate(target_type.kind))
    return;
  resolution->target_type_kind = target_type.kind;
  resolution->target_record_id = target_type.record_id;

  if (psx_type_kind_is_aggregate(operand_type.kind) &&
      operand_type.kind == target_type.kind &&
      operand_type.record_id == target_type.record_id) {
    resolution->status = PSX_AGGREGATE_CAST_STATUS_OK;
    resolution->mode = PSX_AGGREGATE_CAST_COPY_VALUE;
    return;
  }

  if (options->enable_size_compatible_nonscalar_cast &&
      psx_type_kind_is_aggregate(operand_type.kind) && data_layout &&
      record_layouts) {
    int target_size = psx_type_layout_sizeof(types, record_layouts,
                                        target_qual_type.type_id, data_layout);
    int operand_size = psx_type_layout_sizeof(
        types, record_layouts, operand_qual_type.type_id, data_layout);
    if (target_size > 0 && target_size == operand_size &&
        operand_type.kind == target_type.kind) {
      resolution->status = PSX_AGGREGATE_CAST_STATUS_OK;
      resolution->mode = PSX_AGGREGATE_CAST_COPY_VALUE;
      return;
    }
  }

  if (psx_type_kind_is_aggregate(operand_type.kind)) {
    resolution->status = PSX_AGGREGATE_CAST_STATUS_TYPE_MISMATCH;
    return;
  }
  if (target_type.kind == PSX_TYPE_STRUCT &&
      !options->enable_struct_scalar_pointer_cast) {
    resolution->status =
        PSX_AGGREGATE_CAST_STATUS_STRUCT_EXTENSION_DISABLED;
    return;
  }
  if (target_type.kind == PSX_TYPE_UNION &&
      !options->enable_union_scalar_pointer_cast) {
    resolution->status =
        PSX_AGGREGATE_CAST_STATUS_UNION_EXTENSION_DISABLED;
    return;
  }
  if (!psx_type_kind_is_aggregate(target_type.kind)) {
    resolution->status =
        PSX_AGGREGATE_CAST_STATUS_UNSUPPORTED_TARGET;
    return;
  }

  const psx_record_decl_t *record = psx_record_decl_table_lookup(
      record_decls, target_type.record_id);
  if (!record) {
    resolution->status = PSX_AGGREGATE_CAST_STATUS_MEMBER_NOT_FOUND;
    return;
  }
  for (int i = 0; i < record->member_count; i++) {
    if (record->members[i].len <= 0) continue;
    resolution->status = PSX_AGGREGATE_CAST_STATUS_OK;
    resolution->mode = PSX_AGGREGATE_CAST_INITIALIZE_MEMBER;
    resolution->member_qual_type = record->members[i].decl_qual_type;
    resolution->member_index = i;
    return;
  }
  resolution->status = PSX_AGGREGATE_CAST_STATUS_MEMBER_NOT_FOUND;
}
