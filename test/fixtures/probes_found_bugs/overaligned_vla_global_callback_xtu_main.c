// An external function-pointer object must preserve a prototype-scope VLA
// parameter type, including adjusted qualifiers and the element type, across
// translation units. Expected: exit=0.
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#ifndef AG_C_OVERALIGNED_VLA_GLOBAL_CALLBACK_XTU_TYPES
#define AG_C_OVERALIGNED_VLA_GLOBAL_CALLBACK_XTU_TYPES
struct aligned64_vla_callback_cell {
  _Alignas(64) unsigned long long value;
  unsigned int tag;
};
#endif

_Static_assert(_Alignof(struct aligned64_vla_callback_cell) == 64,
               "global VLA callback element alignment");
_Static_assert(sizeof(struct aligned64_vla_callback_cell) == 64,
               "global VLA callback element stride");

typedef unsigned long long vla_global_callback_t(
    int rows, int columns,
    const struct aligned64_vla_callback_cell
        input[static restrict 1][*]);

extern vla_global_callback_t *overaligned_vla_global_callback;

static int is_global_callback_matrix_aligned(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static void exercise_global_vla_callback(
    int source_rows, int source_columns, int use_copy) {
  int rows = source_rows;
  int columns = source_columns;
  unsigned long long before = 0x123456789abcdef0ULL;
  _Alignas(128) struct aligned64_vla_callback_cell
      input[rows][columns];
  unsigned long long after = 0xfedcba9876543210ULL;

  assert(overaligned_vla_global_callback != 0);
  assert(is_global_callback_matrix_aligned(input, 128));
  assert(sizeof input ==
         (size_t)source_rows * (size_t)source_columns *
             sizeof(struct aligned64_vla_callback_cell));

  unsigned long long expected = 0;
  for (int row = 0; row < source_rows; row++) {
    assert(is_global_callback_matrix_aligned(input[row], 64));
    assert((uintptr_t)&input[row][0] - (uintptr_t)&input[0][0] ==
           (uintptr_t)row * (uintptr_t)source_columns *
               sizeof(struct aligned64_vla_callback_cell));
    for (int column = 0; column < source_columns; column++) {
      assert(is_global_callback_matrix_aligned(
          &input[row][column], 64));
      input[row][column].value =
          2000ULL + (unsigned long long)row * 103ULL +
          (unsigned long long)column * 17ULL;
      input[row][column].tag =
          (unsigned int)(row * 11 + column + 5);
      expected += input[row][column].value;
      expected += input[row][column].tag;
    }
  }

  rows = source_rows + 13;
  columns = source_columns + 29;
  unsigned long long actual;
  if (use_copy) {
    vla_global_callback_t *callback =
        overaligned_vla_global_callback;
    actual = callback(source_rows, source_columns, input);
  } else {
    actual = overaligned_vla_global_callback(
        source_rows, source_columns, input);
  }
  assert(actual == expected);
  assert(before == 0x123456789abcdef0ULL);
  assert(after == 0xfedcba9876543210ULL);
}

int main(void) {
  exercise_global_vla_callback(2, 3, 0);
  exercise_global_vla_callback(5, 4, 1);
  exercise_global_vla_callback(3, 9, 0);
  return 0;
}
