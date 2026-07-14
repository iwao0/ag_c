#ifndef PARSER_VLA_RUNTIME_H
#define PARSER_VLA_RUNTIME_H

/* Runtime locations are properties of a declaration/expression, not of its
 * canonical C type. */
typedef struct {
  int row_stride_frame_off;
  int strides_remaining;
} psx_vla_runtime_view_t;

typedef struct {
  psx_vla_runtime_view_t view;
  int row_stride_src_offset;
  int row_stride_elem_size;
  int *param_inner_dim_consts;
  int *param_inner_dim_src_offsets;
  int param_inner_dim_count;
} psx_vla_runtime_descriptor_t;

#endif
