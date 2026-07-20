#include "source_cast_type_resolution.h"

#include <string.h>

#include "../parser/type.h"
#include "type_identity.h"

static psx_source_cast_types_status_t aggregate_cast_status(
    psx_aggregate_cast_status_t status) {
  switch (status) {
    case PSX_AGGREGATE_CAST_STATUS_OK:
      return PSX_SOURCE_CAST_TYPES_OK;
    case PSX_AGGREGATE_CAST_STATUS_TYPE_MISMATCH:
      return PSX_SOURCE_CAST_AGGREGATE_TYPE_MISMATCH;
    case PSX_AGGREGATE_CAST_STATUS_STRUCT_EXTENSION_DISABLED:
      return PSX_SOURCE_CAST_STRUCT_EXTENSION_DISABLED;
    case PSX_AGGREGATE_CAST_STATUS_UNION_EXTENSION_DISABLED:
      return PSX_SOURCE_CAST_UNION_EXTENSION_DISABLED;
    case PSX_AGGREGATE_CAST_STATUS_UNSUPPORTED_TARGET:
      return PSX_SOURCE_CAST_AGGREGATE_UNSUPPORTED;
    case PSX_AGGREGATE_CAST_STATUS_MEMBER_NOT_FOUND:
      return PSX_SOURCE_CAST_AGGREGATE_MEMBER_NOT_FOUND;
    case PSX_AGGREGATE_CAST_STATUS_INVALID:
      return PSX_SOURCE_CAST_TYPES_INVALID;
  }
  return PSX_SOURCE_CAST_TYPES_INVALID;
}

void psx_resolve_source_cast_qual_types(
    const psx_semantic_type_table_t *types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_data_layout_t *data_layout, psx_qual_type_t target_qual_type,
    psx_qual_type_t operand_qual_type, const ag_compilation_options_t *options,
    psx_source_cast_types_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_SOURCE_CAST_TYPES_INVALID;
  resolution->target_qual_type = target_qual_type;
  resolution->operand_qual_type = operand_qual_type;
  if (!types || target_qual_type.type_id == PSX_TYPE_ID_INVALID ||
      operand_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return;

  const psx_type_t *target_type = psx_semantic_type_table_lookup(
      types, target_qual_type.type_id);
  const psx_type_t *operand_type = psx_semantic_type_table_lookup(
      types, operand_qual_type.type_id);
  if (!target_type || !operand_type) return;

  if (target_type->kind == PSX_TYPE_VOID) {
    resolution->status = PSX_SOURCE_CAST_TYPES_OK;
    return;
  }
  if (ps_type_is_tag_aggregate(target_type)) {
    resolution->target_is_aggregate = 1;
    resolution->target_tag_kind = ps_type_tag_token_kind(target_type);
    if (!ps_type_is_tag_aggregate(operand_type) &&
        !ps_type_is_scalar(operand_type)) {
      resolution->status = PSX_SOURCE_CAST_OPERAND_NOT_SCALAR;
      return;
    }
    psx_resolve_aggregate_cast_qual_types(
        types, record_decls, record_layouts, data_layout, target_qual_type,
        operand_qual_type, options, &resolution->aggregate);
    resolution->status = aggregate_cast_status(
        resolution->aggregate.status);
    return;
  }
  if (!ps_type_is_scalar(target_type)) {
    resolution->status =
        PSX_SOURCE_CAST_TARGET_NOT_VOID_OR_SCALAR;
    return;
  }
  if (!ps_type_is_scalar(operand_type)) {
    resolution->status = PSX_SOURCE_CAST_OPERAND_NOT_SCALAR;
    return;
  }
  resolution->status = PSX_SOURCE_CAST_TYPES_OK;
}
