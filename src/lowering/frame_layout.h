#ifndef AGC_LOWERING_FRAME_LAYOUT_H
#define AGC_LOWERING_FRAME_LAYOUT_H

#include "../parser/vla_runtime.h"

typedef struct {
  int next_offset;
} frame_layout_t;

typedef struct {
  int storage_size;
  int row_stride_relative_offset;
  int subsequent_stride_count;
} frame_vla_layout_t;

void frame_layout_reset(frame_layout_t *layout);
void frame_layout_reserve_prefix(frame_layout_t *layout, int bytes);
int frame_layout_allocate(frame_layout_t *layout, int size, int align);

frame_vla_layout_t frame_layout_vla_storage(int dim_count, int inner_is_const);
frame_vla_layout_t frame_layout_pointer_vla_storage(void);
int frame_layout_vla_stride_offset(int base_offset, int level);
int frame_layout_pointer_vla_stride_offset(int base_offset);

#endif
