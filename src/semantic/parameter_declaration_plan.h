#ifndef SEMANTIC_PARAMETER_DECLARATION_PLAN_H
#define SEMANTIC_PARAMETER_DECLARATION_PLAN_H

#include "../parser/type.h"
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

int psx_plan_parameter_storage(
    const psx_type_t *type, psx_parameter_storage_plan_t *plan);
int psx_plan_parameter_storage_for_target(
    const psx_type_t *type, const ag_target_info_t *target,
    psx_parameter_storage_plan_t *plan);

#endif
