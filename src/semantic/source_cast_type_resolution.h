#ifndef SEMANTIC_SOURCE_CAST_TYPE_RESOLUTION_H
#define SEMANTIC_SOURCE_CAST_TYPE_RESOLUTION_H

#include "aggregate_cast_resolution.h"

typedef enum {
  PSX_SOURCE_CAST_TYPES_OK = 0,
  PSX_SOURCE_CAST_TYPES_INVALID,
  PSX_SOURCE_CAST_TARGET_NOT_VOID_OR_SCALAR,
  PSX_SOURCE_CAST_OPERAND_NOT_SCALAR,
  PSX_SOURCE_CAST_SCALAR_CATEGORIES_INCOMPATIBLE,
  PSX_SOURCE_CAST_AGGREGATE_TYPE_MISMATCH,
  PSX_SOURCE_CAST_STRUCT_EXTENSION_DISABLED,
  PSX_SOURCE_CAST_UNION_EXTENSION_DISABLED,
  PSX_SOURCE_CAST_AGGREGATE_UNSUPPORTED,
  PSX_SOURCE_CAST_AGGREGATE_MEMBER_NOT_FOUND,
} psx_source_cast_types_status_t;

typedef struct {
  psx_source_cast_types_status_t status;
  psx_qual_type_t target_qual_type;
  psx_qual_type_t operand_qual_type;
  int target_is_aggregate;
  psx_type_kind_t target_type_kind;
  psx_aggregate_cast_resolution_t aggregate;
} psx_source_cast_types_resolution_t;

void psx_resolve_source_cast_qual_types(
    const psx_semantic_type_table_t *types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_data_layout_t *data_layout, psx_qual_type_t target_qual_type,
    psx_qual_type_t operand_qual_type, const ag_compilation_options_t *options,
    psx_source_cast_types_resolution_t *resolution);

#endif
