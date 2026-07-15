#ifndef SEMANTIC_PARAMETER_DECLARATION_PLAN_H
#define SEMANTIC_PARAMETER_DECLARATION_PLAN_H

#include "type_identity.h"
#include "../target_info.h"

typedef enum {
  PSX_PARAMETER_STORAGE_SCALAR = 0,
  PSX_PARAMETER_STORAGE_POINTER,
  PSX_PARAMETER_STORAGE_AGGREGATE_VALUE,
  PSX_PARAMETER_STORAGE_AGGREGATE_BYREF,
  PSX_PARAMETER_STORAGE_COMPLEX,
} psx_parameter_storage_kind_t;

typedef struct {
  psx_parameter_storage_kind_t kind;
  int storage_size;
  int alignment;
  int is_byref;
} psx_parameter_storage_plan_t;

int psx_plan_parameter_storage_for_type_id(
    const psx_semantic_type_table_t *types, psx_type_id_t type_id,
    const ag_target_info_t *target,
    psx_parameter_storage_plan_t *plan);

#endif
