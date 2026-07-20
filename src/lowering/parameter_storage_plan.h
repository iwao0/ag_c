#ifndef LOWERING_PARAMETER_STORAGE_PLAN_H
#define LOWERING_PARAMETER_STORAGE_PLAN_H

#include "../semantic/record_layout.h"
#include "../semantic/type_identity.h"

typedef struct ag_data_layout_t ag_data_layout_t;
typedef struct ir_abi_target_policy_t ir_abi_target_policy_t;

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
    const psx_semantic_type_table_t *types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_data_layout_t *data_layout,
    const ir_abi_target_policy_t *abi_policy,
    psx_parameter_storage_plan_t *plan);

#endif
