// Definition side of overaligned_vla_global_callback_xtu_main.c.
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

static int is_global_callback_input_aligned(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static unsigned long long sum_overaligned_vla_callback(
    int rows, int columns,
    const struct aligned64_vla_callback_cell
        (*restrict input)[columns]) {
  assert(sizeof *input ==
         (size_t)columns *
             sizeof(struct aligned64_vla_callback_cell));
  assert(is_global_callback_input_aligned(input, 64));

  unsigned long long total = 0;
  for (int row = 0; row < rows; row++) {
    assert(is_global_callback_input_aligned(input + row, 64));
    assert((uintptr_t)(input + row) - (uintptr_t)input ==
           (uintptr_t)row * (uintptr_t)columns *
               sizeof(struct aligned64_vla_callback_cell));
    for (int column = 0; column < columns; column++) {
      assert(is_global_callback_input_aligned(
          &input[row][column], 64));
      total += input[row][column].value;
      total += input[row][column].tag;
    }
  }
  return total;
}

unsigned long long (*overaligned_vla_global_callback)(
    int, int,
    const struct aligned64_vla_callback_cell (*)[*]) =
    sum_overaligned_vla_callback;
