// A factory-produced callback combines a runtime VLA parameter with an
// over-aligned aggregate return that uses a hidden return destination.
// Expected: exit=0.
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "test/fixtures/probes_found_bugs/overaligned_vla_callback_aggregate_return_xtu.h"

_Static_assert(_Alignof(struct aligned64_vla_return_cell) == 64,
               "VLA callback aggregate-return element alignment");
_Static_assert(sizeof(struct aligned64_vla_return_cell) == 64,
               "VLA callback aggregate-return element stride");
_Static_assert(_Alignof(struct aligned64_vla_callback_result) == 64,
               "VLA callback aggregate-return result alignment");
_Static_assert(sizeof(struct aligned64_vla_callback_result) == 64,
               "VLA callback aggregate-return result size");

static int is_aggregate_return_matrix_aligned(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static void verify_result(
    const struct aligned64_vla_callback_result *result,
    unsigned long long expected_sum,
    unsigned long long expected_count,
    unsigned long long expected_edge) {
  assert(is_aggregate_return_matrix_aligned(result, 64));
  assert(result->sum == expected_sum);
  assert(result->count == expected_count);
  assert(result->edge == expected_edge);
}

static void exercise_aggregate_return_factory(
    int source_rows, int source_columns) {
  int rows = source_rows;
  int columns = source_columns;
  unsigned long long before = 0x1029384756abcdefULL;
  _Alignas(128) struct aligned64_vla_return_cell
      input[rows][columns];
  unsigned long long after = 0xfedcba6547382910ULL;

  assert(is_aggregate_return_matrix_aligned(input, 128));
  assert(sizeof input ==
         (size_t)source_rows * (size_t)source_columns *
             sizeof(struct aligned64_vla_return_cell));

  unsigned long long expected_sum = 0;
  unsigned long long expected_count =
      (unsigned long long)source_rows *
      (unsigned long long)source_columns;
  for (int row = 0; row < source_rows; row++) {
    assert(is_aggregate_return_matrix_aligned(input[row], 64));
    for (int column = 0; column < source_columns; column++) {
      assert(is_aggregate_return_matrix_aligned(
          &input[row][column], 64));
      input[row][column].value =
          5000ULL + (unsigned long long)row * 131ULL +
          (unsigned long long)column * 23ULL;
      input[row][column].tag =
          (unsigned int)(row * 17 + column + 11);
      expected_sum += input[row][column].value;
      expected_sum += input[row][column].tag;
    }
  }
  unsigned long long expected_edge =
      input[0][0].value ^
      input[source_rows - 1][source_columns - 1].value;

  rows += 19;
  columns += 37;

  vla_aggregate_return_callback_t *direct_callback =
      select_overaligned_vla_aggregate_return_callback();
  assert(direct_callback != 0);
  struct aligned64_vla_callback_result direct =
      direct_callback(source_rows, source_columns, input);
  verify_result(&direct, expected_sum, expected_count, expected_edge);

  assert(overaligned_vla_aggregate_return_factory != 0);
  vla_aggregate_return_callback_t *global_callback =
      overaligned_vla_aggregate_return_factory();
  assert(global_callback != 0);
  struct aligned64_vla_callback_result global =
      global_callback(source_rows, source_columns, input);
  verify_result(&global, expected_sum, expected_count, expected_edge);

  vla_aggregate_return_factory_t *factory_copy =
      overaligned_vla_aggregate_return_factory;
  vla_aggregate_return_callback_t *copied_callback = factory_copy();
  assert(copied_callback != 0);
  struct aligned64_vla_callback_result copied =
      copied_callback(source_rows, source_columns, input);
  verify_result(&copied, expected_sum, expected_count, expected_edge);

  assert(before == 0x1029384756abcdefULL);
  assert(after == 0xfedcba6547382910ULL);
}

int main(void) {
  exercise_aggregate_return_factory(2, 3);
  exercise_aggregate_return_factory(4, 5);
  exercise_aggregate_return_factory(3, 8);
  return 0;
}
