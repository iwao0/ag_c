#ifndef SEMANTIC_AGGREGATE_CAST_RESOLUTION_H
#define SEMANTIC_AGGREGATE_CAST_RESOLUTION_H

#include "../compilation_options.h"
#include "../target_info.h"
#include "../type_system/type_ids.h"
#include "../type_system/type_shape.h"

typedef struct psx_record_decl_table_t psx_record_decl_table_t;
typedef struct psx_record_layout_table_t psx_record_layout_table_t;
typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;

typedef enum {
  PSX_AGGREGATE_CAST_STATUS_OK = 0,
  PSX_AGGREGATE_CAST_STATUS_INVALID,
  PSX_AGGREGATE_CAST_STATUS_TYPE_MISMATCH,
  PSX_AGGREGATE_CAST_STATUS_STRUCT_EXTENSION_DISABLED,
  PSX_AGGREGATE_CAST_STATUS_UNION_EXTENSION_DISABLED,
  PSX_AGGREGATE_CAST_STATUS_UNSUPPORTED_TARGET,
  PSX_AGGREGATE_CAST_STATUS_MEMBER_NOT_FOUND,
} psx_aggregate_cast_status_t;

typedef enum {
  PSX_AGGREGATE_CAST_COPY_VALUE = 0,
  PSX_AGGREGATE_CAST_INITIALIZE_MEMBER,
} psx_aggregate_cast_mode_t;

typedef struct {
  psx_aggregate_cast_status_t status;
  psx_aggregate_cast_mode_t mode;
  psx_qual_type_t target_qual_type;
  psx_qual_type_t member_qual_type;
  psx_record_id_t target_record_id;
  int member_index;
  psx_type_kind_t target_type_kind;
} psx_aggregate_cast_resolution_t;

void psx_resolve_aggregate_cast_qual_types(
    const psx_semantic_type_table_t *types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_data_layout_t *data_layout, psx_qual_type_t target_qual_type,
    psx_qual_type_t operand_qual_type, const ag_compilation_options_t *options,
    psx_aggregate_cast_resolution_t *resolution);

#endif
