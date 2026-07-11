#include "local_storage.h"

#include "frame_layout.h"

static frame_layout_t current_layout;

void local_storage_reset(void) {
  frame_layout_reset(&current_layout);
}

void local_storage_reserve_prefix(int bytes) {
  frame_layout_reserve_prefix(&current_layout, bytes);
}

int local_storage_allocate(int size, int align) {
  return frame_layout_allocate(&current_layout, size, align);
}
