#ifndef SEMANTIC_DECLARATOR_APPLICATION_TYPES_H
#define SEMANTIC_DECLARATOR_APPLICATION_TYPES_H

#include "../parser/declarator_shape.h"
#include "expression_identity.h"

typedef struct {
  int declarator_op_index;
  psx_semantic_expr_id_t expression_id;
  long long constant_value;
  int is_constant;
} psx_runtime_array_bound_t;

typedef struct psx_runtime_declarator_application_t {
  psx_declarator_shape_t shape;
  psx_runtime_array_bound_t *array_bounds;
  int array_bound_count;
} psx_runtime_declarator_application_t;

#endif
