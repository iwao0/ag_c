#ifndef SEMANTIC_LOCAL_DECLARATION_PLAN_H
#define SEMANTIC_LOCAL_DECLARATION_PLAN_H

#include "../parser/type.h"

typedef struct {
  int storage_size;
  int scalar_element_size;
  int alignment;
} psx_complete_array_storage_plan_t;

typedef struct {
  int storage_size;
  int element_size;
  int alignment;
} psx_complete_object_storage_plan_t;

int psx_plan_complete_array_storage(
    const psx_type_t *type, psx_complete_array_storage_plan_t *out);
int psx_plan_complete_object_storage(
    const psx_type_t *type, psx_complete_object_storage_plan_t *out);

#endif
