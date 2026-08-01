// A factory result callback may itself carry a prototype-scope VLA parameter
// through both a function symbol and an external factory-pointer object.
// Expected: exit=0.
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "test/fixtures/probes_found_bugs/overaligned_vla_callback_factory_xtu.h"

_Static_assert(_Alignof(struct aligned64_vla_factory_cell) == 64,
               "VLA factory callback element alignment");
_Static_assert(sizeof(struct aligned64_vla_factory_cell) == 64,
               "VLA factory callback element stride");

static int is_factory_matrix_aligned(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static void exercise_vla_callback_factory(
    int source_rows, int source_columns) {
  int rows = source_rows;
  int columns = source_columns;
  unsigned long long before = 0x13579bdf2468ace0ULL;
  _Alignas(128) struct aligned64_vla_factory_cell
      input[rows][columns];
  unsigned long long after = 0xeca86420fdb97531ULL;

  assert(is_factory_matrix_aligned(input, 128));
  assert(sizeof input ==
         (size_t)source_rows * (size_t)source_columns *
             sizeof(struct aligned64_vla_factory_cell));

  unsigned long long expected = 0;
  for (int row = 0; row < source_rows; row++) {
    assert(is_factory_matrix_aligned(input[row], 64));
    assert((uintptr_t)&input[row][0] - (uintptr_t)&input[0][0] ==
           (uintptr_t)row * (uintptr_t)source_columns *
               sizeof(struct aligned64_vla_factory_cell));
    for (int column = 0; column < source_columns; column++) {
      assert(is_factory_matrix_aligned(&input[row][column], 64));
      input[row][column].value =
          3000ULL + (unsigned long long)row * 107ULL +
          (unsigned long long)column * 19ULL;
      input[row][column].tag =
          (unsigned int)(row * 13 + column + 7);
      expected += input[row][column].value;
      expected += input[row][column].tag;
    }
  }

  rows = source_rows + 17;
  columns = source_columns + 31;
  vla_factory_callback_t *from_direct_factory =
      select_overaligned_vla_callback();
  assert(from_direct_factory != 0);
  assert(from_direct_factory(
             source_rows, source_columns, input) == expected);

  assert(overaligned_vla_callback_factory != 0);
  vla_factory_callback_t *from_global_factory =
      overaligned_vla_callback_factory();
  assert(from_global_factory != 0);
  assert(from_global_factory(
             source_rows, source_columns, input) == expected);

  vla_callback_factory_t *factory_copy =
      overaligned_vla_callback_factory;
  vla_factory_callback_t *from_factory_copy = factory_copy();
  assert(from_factory_copy != 0);
  assert(from_factory_copy(
             source_rows, source_columns, input) == expected);

  assert(before == 0x13579bdf2468ace0ULL);
  assert(after == 0xeca86420fdb97531ULL);
}

int main(void) {
  exercise_vla_callback_factory(2, 3);
  exercise_vla_callback_factory(4, 5);
  exercise_vla_callback_factory(3, 8);
  return 0;
}
