#ifndef SEMANTIC_LOCAL_DECLARATION_PLAN_H
#define SEMANTIC_LOCAL_DECLARATION_PLAN_H

#include "type_identity.h"
#include "../target_info.h"

typedef struct {
  int storage_size;
  int alignment;
} psx_local_storage_plan_t;

int psx_plan_local_storage_for_type_id(
    const psx_semantic_type_table_t *types, psx_type_id_t type_id,
    const ag_target_info_t *target,
    psx_local_storage_plan_t *out);

#endif
