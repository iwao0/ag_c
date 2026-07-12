#ifndef SEMANTIC_GLOBAL_DECLARATION_PLAN_H
#define SEMANTIC_GLOBAL_DECLARATION_PLAN_H

#include "../parser/type.h"

typedef struct {
  int storage_size;
  int is_incomplete_array;
} psx_global_storage_plan_t;

int psx_plan_global_object_storage(
    const psx_type_t *type, psx_global_storage_plan_t *plan);

#endif
