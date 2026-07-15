#ifndef SEMANTIC_PARAMETER_DECLARATION_RESOLUTION_H
#define SEMANTIC_PARAMETER_DECLARATION_RESOLUTION_H

#include "declaration_resolution.h"
#include "parameter_declaration_plan.h"

typedef struct {
  psx_semantic_expr_id_t expression_id;
  long long constant_value;
  int is_constant;
} psx_parameter_dimension_t;

typedef enum {
  PSX_PARAMETER_LOWER_NORMAL = 0,
  PSX_PARAMETER_LOWER_VLA,
} psx_parameter_lowering_kind_t;

typedef struct {
  psx_decl_type_request_t type;
  psx_parameter_dimension_t *inner_dimensions;
  int inner_dimension_count;
} psx_parameter_declaration_resolution_request_t;

typedef struct {
  const psx_type_t *type;
  psx_parameter_storage_plan_t storage;
  psx_parameter_lowering_kind_t lowering_kind;
  psx_type_id_t runtime_stride_storage_type_id;
  psx_parameter_dimension_t *inner_dimensions;
  int inner_dimension_count;
} psx_parameter_declaration_resolution_t;

int psx_resolve_parameter_declaration(
    const psx_parameter_declaration_resolution_request_t *request,
    psx_parameter_declaration_resolution_t *resolution);

#endif
