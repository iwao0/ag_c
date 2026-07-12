#include "frame_layout.h"

void frame_layout_reset(frame_layout_t *layout) {
  if (layout) layout->next_offset = 0;
}

void frame_layout_reserve_prefix(frame_layout_t *layout, int bytes) {
  if (layout && bytes > layout->next_offset) layout->next_offset = bytes;
}

int frame_layout_allocate(frame_layout_t *layout, int size, int align) {
  if (!layout) return 0;
  if (align > 1) {
    layout->next_offset = (layout->next_offset + align - 1) & ~(align - 1);
  }
  int offset = layout->next_offset;
  layout->next_offset += size;
  return offset;
}

frame_vla_layout_t frame_layout_vla_storage(int dim_count, int inner_is_const) {
  frame_vla_layout_t layout = {16, 0, 0};
  if (dim_count <= 1 || (dim_count == 2 && inner_is_const)) return layout;

  int stride_count = dim_count - 1;
  layout.storage_size += 8 * stride_count;
  layout.row_stride_relative_offset = 16;
  layout.subsequent_stride_count = stride_count - 1;
  return layout;
}

frame_vla_layout_t frame_layout_pointer_vla_storage(void) {
  frame_vla_layout_t layout = {16, 8, 0};
  return layout;
}

int frame_layout_vla_stride_offset(int base_offset, int level) {
  return base_offset + 16 + 8 * level;
}

int frame_layout_pointer_vla_stride_offset(int base_offset) {
  return base_offset + 8;
}
