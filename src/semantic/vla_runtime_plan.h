#ifndef SEMANTIC_VLA_RUNTIME_PLAN_H
#define SEMANTIC_VLA_RUNTIME_PLAN_H

#include "typed_hir_tree.h"

typedef struct {
  const psx_typed_hir_tree_t *expression;
  long long constant_value;
  unsigned char is_constant;
} psx_vla_runtime_dimension_t;

typedef struct psx_vla_runtime_plan_t {
  psx_vla_runtime_dimension_t *dimensions;
  psx_qual_type_t constant_qual_type;
  int *stride_store_offsets;
  int *stride_start_dimensions;
  int dimension_count;
  int stride_store_count;
  int descriptor_frame_offset;
  int row_stride_frame_offset;
  int element_size;
  unsigned char performs_allocation;
} psx_vla_runtime_plan_t;

#endif
