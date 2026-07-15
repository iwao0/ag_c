#include "local_storage.h"

#include "frame_layout.h"
#include "runtime_context.h"

void local_storage_reset(psx_lowering_context_t *context) {
  if (!context) return;
  frame_layout_reset(&context->local_frame_layout);
}

void local_storage_reserve_prefix(
    psx_lowering_context_t *context, int bytes) {
  if (!context) return;
  frame_layout_reserve_prefix(&context->local_frame_layout, bytes);
}

int local_storage_allocate(
    psx_lowering_context_t *context, int size, int align) {
  if (!context) return 0;
  return frame_layout_allocate(&context->local_frame_layout, size, align);
}
