// Definition side of overaligned_vla_callback_factory_xtu_main.c.
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "test/fixtures/probes_found_bugs/overaligned_vla_callback_factory_xtu.h"

static int is_factory_input_aligned(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static unsigned long long sum_factory_vla_callback(
    int rows, int columns,
    const struct aligned64_vla_factory_cell
        (*restrict input)[columns]) {
  assert(sizeof *input ==
         (size_t)columns *
             sizeof(struct aligned64_vla_factory_cell));
  assert(is_factory_input_aligned(input, 64));

  unsigned long long total = 0;
  for (int row = 0; row < rows; row++) {
    assert(is_factory_input_aligned(input + row, 64));
    assert((uintptr_t)(input + row) - (uintptr_t)input ==
           (uintptr_t)row * (uintptr_t)columns *
               sizeof(struct aligned64_vla_factory_cell));
    for (int column = 0; column < columns; column++) {
      assert(is_factory_input_aligned(&input[row][column], 64));
      total += input[row][column].value;
      total += input[row][column].tag;
    }
  }
  return total;
}

vla_factory_callback_t *select_overaligned_vla_callback(void) {
  return sum_factory_vla_callback;
}

vla_callback_factory_t *overaligned_vla_callback_factory =
    select_overaligned_vla_callback;
