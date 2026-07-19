#include "aggregate_cast_resolution.h"

#include <string.h>

#include "../parser/type.h"
#include "../type_layout.h"
#include "record_decl_table.h"
#include "record_layout.h"
#include "type_identity.h"

void psx_resolve_aggregate_cast_qual_types(
    const psx_semantic_type_table_t *types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    psx_qual_type_t target_qual_type,
    psx_qual_type_t operand_qual_type,
    const ag_compilation_options_t *options,
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

  const psx_type_t *target_type = psx_semantic_type_table_lookup(
      types, target_qual_type.type_id);
  const psx_type_t *operand_type = psx_semantic_type_table_lookup(
      types, operand_qual_type.type_id);
  if (!ps_type_is_tag_aggregate(target_type) || !operand_type)
    return;
  resolution->target_tag_kind = ps_type_tag_token_kind(target_type);

  if (ps_type_is_tag_aggregate(operand_type) &&
      ps_type_tag_identity_matches(operand_type, target_type)) {
    resolution->status = PSX_AGGREGATE_CAST_STATUS_OK;
    resolution->mode = PSX_AGGREGATE_CAST_COPY_VALUE;
    return;
  }

  if (options->enable_size_compatible_nonscalar_cast &&
      ps_type_is_tag_aggregate(operand_type) && target &&
      record_layouts) {
    int target_size = ps_type_sizeof_id(
        types, record_layouts, target_qual_type.type_id, target);
    int operand_size = ps_type_sizeof_id(
        types, record_layouts, operand_qual_type.type_id, target);
    if (target_size > 0 && target_size == operand_size &&
        (int)ps_type_tag_token_kind(operand_type) ==
            resolution->target_tag_kind) {
      resolution->status = PSX_AGGREGATE_CAST_STATUS_OK;
      resolution->mode = PSX_AGGREGATE_CAST_COPY_VALUE;
      return;
    }
  }

  if (ps_type_is_tag_aggregate(operand_type)) {
    resolution->status = PSX_AGGREGATE_CAST_STATUS_TYPE_MISMATCH;
    return;
  }
  if (resolution->target_tag_kind == TK_STRUCT &&
      !options->enable_struct_scalar_pointer_cast) {
    resolution->status =
        PSX_AGGREGATE_CAST_STATUS_STRUCT_EXTENSION_DISABLED;
    return;
  }
  if (resolution->target_tag_kind == TK_UNION &&
      !options->enable_union_scalar_pointer_cast) {
    resolution->status =
        PSX_AGGREGATE_CAST_STATUS_UNION_EXTENSION_DISABLED;
    return;
  }
  if (resolution->target_tag_kind != TK_STRUCT &&
      resolution->target_tag_kind != TK_UNION) {
    resolution->status =
        PSX_AGGREGATE_CAST_STATUS_UNSUPPORTED_TARGET;
    return;
  }

  const psx_record_decl_t *record = psx_record_decl_table_lookup(
      record_decls, ps_type_record_id(target_type));
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
