#ifndef PARSER_VLA_RUNTIME_H
#define PARSER_VLA_RUNTIME_H

#define PSX_VLA_RUNTIME_MAX_INNER_DIMS 7

/* Runtime locations are properties of a declaration/expression, not of its
 * canonical C type. */
typedef struct {
  int row_stride_frame_off;
  int strides_remaining;
} psx_vla_runtime_view_t;

typedef struct {
  psx_vla_runtime_view_t view;
  int row_stride_src_offset;
  short row_stride_elem_size;
  short param_inner_dim_consts[PSX_VLA_RUNTIME_MAX_INNER_DIMS];
  int param_inner_dim_src_offsets[PSX_VLA_RUNTIME_MAX_INNER_DIMS];
  unsigned char param_inner_dim_count;
} psx_vla_runtime_descriptor_t;

#endif
