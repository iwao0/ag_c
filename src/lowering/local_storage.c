#include "local_storage.h"

#include "frame_layout.h"
#include "runtime_context.h"

void local_storage_reset(void) {
  frame_layout_reset(
      &ps_lowering_context_active()->local_frame_layout);
}

void local_storage_reserve_prefix(int bytes) {
  frame_layout_reserve_prefix(
      &ps_lowering_context_active()->local_frame_layout, bytes);
}

int local_storage_allocate(int size, int align) {
  return frame_layout_allocate(
      &ps_lowering_context_active()->local_frame_layout, size, align);
}
