#ifndef SEMANTIC_STATIC_INITIALIZER_CLASSIFICATION_H
#define SEMANTIC_STATIC_INITIALIZER_CLASSIFICATION_H

#include "type_identity.h"

typedef enum {
  PSX_STATIC_INITIALIZER_OK = 0,
  PSX_STATIC_INITIALIZER_INVALID,
  PSX_STATIC_INITIALIZER_DUPLICATE_DEFINITION,
  PSX_STATIC_INITIALIZER_ARRAY_COMPLETION_FAILED,
  PSX_STATIC_INITIALIZER_INVALID_SCALAR_LIST,
} psx_static_initializer_status_t;

typedef struct {
  psx_static_initializer_status_t status;
  psx_qual_type_t object_qual_type;
  int is_aggregate_initializer;
  int type_completed;
  int scalar_list_value_selected;
} psx_static_initializer_resolution_t;

#endif
