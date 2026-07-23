#ifndef PARSER_VLA_RUNTIME_H
#define PARSER_VLA_RUNTIME_H

/* Internal frame ABI. These fixed-width slots are independent from C scalar
 * and pointer layout selected by TargetSpec. */
enum {
  PSX_VLA_RUNTIME_SLOT_SIZE = 8,
  PSX_VLA_RUNTIME_SIZE_RELATIVE_OFFSET = PSX_VLA_RUNTIME_SLOT_SIZE,
  PSX_VLA_RUNTIME_FIRST_STRIDE_RELATIVE_OFFSET =
      2 * PSX_VLA_RUNTIME_SLOT_SIZE,
  PSX_VLA_RUNTIME_DESCRIPTOR_HEADER_SIZE =
      PSX_VLA_RUNTIME_FIRST_STRIDE_RELATIVE_OFFSET,
  PSX_POINTER_VLA_RUNTIME_STRIDE_RELATIVE_OFFSET =
      PSX_VLA_RUNTIME_SLOT_SIZE,
};

static inline int psx_vla_runtime_stride_relative_offset(int level) {
  return PSX_VLA_RUNTIME_FIRST_STRIDE_RELATIVE_OFFSET +
         PSX_VLA_RUNTIME_SLOT_SIZE * level;
}

/* Runtime locations are properties of a declaration/expression, not of its
 * canonical C type. */
typedef struct {
  int row_stride_frame_off;
  int strides_remaining;
  int pointer_indirections;
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
