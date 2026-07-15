#ifndef SEMANTIC_LOCAL_DECLARATION_PLAN_H
#define SEMANTIC_LOCAL_DECLARATION_PLAN_H

#include "../parser/type.h"
#include "../target_info.h"

typedef struct {
  int storage_size;
  int alignment;
} psx_local_storage_plan_t;

int psx_plan_local_storage(
    const psx_type_t *type, psx_local_storage_plan_t *out);
int psx_plan_local_storage_for_target(
    const psx_type_t *type, const ag_target_info_t *target,
    psx_local_storage_plan_t *out);

#endif
