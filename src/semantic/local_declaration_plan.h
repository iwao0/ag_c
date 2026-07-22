#ifndef SEMANTIC_LOCAL_DECLARATION_PLAN_H
#define SEMANTIC_LOCAL_DECLARATION_PLAN_H

#include "record_layout.h"
#include "type_identity.h"
#include "../target_info.h"

typedef struct {
  int storage_size;
  int alignment;
} psx_local_storage_plan_t;

int psx_plan_local_storage_for_type_id(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts, psx_type_id_t type_id,
    const ag_data_layout_t *data_layout, psx_local_storage_plan_t *out);
int psx_plan_local_storage_for_qual_type(
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_qual_type_t qual_type, const ag_data_layout_t *data_layout,
    psx_local_storage_plan_t *out);

#endif
