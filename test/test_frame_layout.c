#include "../src/lowering/frame_layout.h"
#include "../src/lowering/local_storage.h"
#include <stdio.h>

#define ASSERT_EQ(expected, actual)                                                \
  do {                                                                            \
    int actual_value = (actual);                                                   \
    if ((expected) != actual_value) {                                              \
      fprintf(stderr, "%s:%d: expected %d, got %d\n", __FILE__, __LINE__,        \
              (expected), actual_value);                                           \
      return 1;                                                                    \
    }                                                                              \
  } while (0)

static int test_object_layout(void) {
  frame_layout_t layout;
  frame_layout_reset(&layout);
  ASSERT_EQ(0, frame_layout_allocate(&layout, 4, 4));
  ASSERT_EQ(8, frame_layout_allocate(&layout, 8, 8));

  frame_layout_reset(&layout);
  frame_layout_reserve_prefix(&layout, 64);
  frame_layout_reserve_prefix(&layout, 32);
  ASSERT_EQ(64, frame_layout_allocate(&layout, 8, 8));
  return 0;
}

static int test_vla_layout(void) {
  frame_vla_layout_t one_dim = frame_layout_vla_storage(1, 0);
  ASSERT_EQ(16, one_dim.storage_size);
  ASSERT_EQ(0, one_dim.row_stride_relative_offset);

  frame_vla_layout_t const_inner = frame_layout_vla_storage(2, 1);
  ASSERT_EQ(16, const_inner.storage_size);

  frame_vla_layout_t runtime_inner = frame_layout_vla_storage(2, 0);
  ASSERT_EQ(24, runtime_inner.storage_size);
  ASSERT_EQ(16, runtime_inner.row_stride_relative_offset);
  ASSERT_EQ(0, runtime_inner.subsequent_stride_count);

  frame_vla_layout_t three_dim = frame_layout_vla_storage(3, 0);
  ASSERT_EQ(32, three_dim.storage_size);
  ASSERT_EQ(1, three_dim.subsequent_stride_count);
  ASSERT_EQ(56, frame_layout_vla_stride_offset(32, 1));
  ASSERT_EQ(40, frame_layout_pointer_vla_stride_offset(32));
  return 0;
}

static int test_current_local_storage(void) {
  local_storage_reset();
  ASSERT_EQ(0, local_storage_allocate(4, 4));
  ASSERT_EQ(8, local_storage_allocate(8, 8));

  local_storage_reset();
  local_storage_reserve_prefix(64);
  ASSERT_EQ(64, local_storage_allocate(8, 8));
  return 0;
}

int main(void) {
  if (test_object_layout() != 0) return 1;
  if (test_vla_layout() != 0) return 1;
  if (test_current_local_storage() != 0) return 1;
  printf("frame layout tests passed\n");
  return 0;
}
