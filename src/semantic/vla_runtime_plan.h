#ifndef SEMANTIC_VLA_RUNTIME_PLAN_H
#define SEMANTIC_VLA_RUNTIME_PLAN_H

#include "../parser/node_fwd.h"

typedef struct psx_vla_runtime_plan_t {
  node_t **dimensions;
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
