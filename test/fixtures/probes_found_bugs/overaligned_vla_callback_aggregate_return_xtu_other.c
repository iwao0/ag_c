// Definition side of overaligned_vla_callback_aggregate_return_xtu_main.c.
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "test/fixtures/probes_found_bugs/overaligned_vla_callback_aggregate_return_xtu.h"

static int is_aggregate_return_input_aligned(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static struct aligned64_vla_callback_result
summarize_overaligned_vla_for_aggregate_return(
    int rows, int columns,
    const struct aligned64_vla_return_cell
        (*restrict input)[columns]) {
  assert(sizeof *input ==
         (size_t)columns *
             sizeof(struct aligned64_vla_return_cell));
  assert(is_aggregate_return_input_aligned(input, 64));

  struct aligned64_vla_callback_result result = {0, 0, 0};
  for (int row = 0; row < rows; row++) {
    assert(is_aggregate_return_input_aligned(input + row, 64));
    assert((uintptr_t)(input + row) - (uintptr_t)input ==
           (uintptr_t)row * (uintptr_t)columns *
               sizeof(struct aligned64_vla_return_cell));
    for (int column = 0; column < columns; column++) {
      assert(is_aggregate_return_input_aligned(
          &input[row][column], 64));
      result.sum += input[row][column].value;
      result.sum += input[row][column].tag;
      result.count++;
    }
  }
  result.edge =
      input[0][0].value ^ input[rows - 1][columns - 1].value;
  return result;
}

vla_aggregate_return_callback_t
    *select_overaligned_vla_aggregate_return_callback(void) {
  return summarize_overaligned_vla_for_aggregate_return;
}

vla_aggregate_return_factory_t
    *overaligned_vla_aggregate_return_factory =
        select_overaligned_vla_aggregate_return_callback;
