// A prototype-scope multidimensional VLA parameter with static/restrict
// adjustment must remain compatible with a pointer-to-VLA definition in a
// separate translation unit. Expected: exit=0.
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#ifndef AG_C_OVERALIGNED_VLA_PARAMETER_QUALIFIER_XTU_TYPES
#define AG_C_OVERALIGNED_VLA_PARAMETER_QUALIFIER_XTU_TYPES
struct aligned64_vla_parameter_cell {
  _Alignas(64) unsigned long long value;
  unsigned int tag;
};
#endif

_Static_assert(_Alignof(struct aligned64_vla_parameter_cell) == 64,
               "cross-TU VLA parameter element alignment");
_Static_assert(sizeof(struct aligned64_vla_parameter_cell) == 64,
               "cross-TU VLA parameter element stride");

typedef unsigned long long matrix_transform_t(
    int rows, int columns,
    const struct aligned64_vla_parameter_cell
        input[static restrict 1][*],
    struct aligned64_vla_parameter_cell
        output[static restrict 1][*]);

extern unsigned long long transform_overaligned_vla_parameter(
    int rows, int columns,
    const struct aligned64_vla_parameter_cell
        input[static restrict rows][columns],
    struct aligned64_vla_parameter_cell
        output[static restrict rows][columns]);

static int is_main_matrix_aligned(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static void verify_transformed_matrix(
    int rows, int columns,
    const struct aligned64_vla_parameter_cell input[rows][columns],
    const struct aligned64_vla_parameter_cell output[rows][columns]) {
  for (int row = 0; row < rows; row++) {
    assert(is_main_matrix_aligned(input[row], 64));
    assert(is_main_matrix_aligned(output[row], 64));
    for (int column = 0; column < columns; column++) {
      assert(is_main_matrix_aligned(&input[row][column], 64));
      assert(is_main_matrix_aligned(&output[row][column], 64));
      assert(output[row][column].value ==
             input[row][column].value +
                 (unsigned long long)row * 17ULL +
                 (unsigned long long)column * 5ULL);
      assert(output[row][column].tag ==
             input[row][column].tag +
                 (unsigned int)(row + column + 1));
    }
  }
}

static void exercise_cross_tu_vla_parameter(
    int source_rows, int source_columns) {
  int rows = source_rows;
  int columns = source_columns;
  unsigned long long before = 0x1122334455667788ULL;
  _Alignas(128) struct aligned64_vla_parameter_cell
      input[rows][columns];
  _Alignas(128) struct aligned64_vla_parameter_cell
      output[rows][columns];
  unsigned long long after = 0x8877665544332211ULL;

  assert(is_main_matrix_aligned(input, 128));
  assert(is_main_matrix_aligned(output, 128));
  assert(sizeof input ==
         (size_t)source_rows * (size_t)source_columns *
             sizeof(struct aligned64_vla_parameter_cell));

  unsigned long long expected = 0;
  for (int row = 0; row < source_rows; row++) {
    assert((uintptr_t)&input[row][0] - (uintptr_t)&input[0][0] ==
           (uintptr_t)row * (uintptr_t)source_columns *
               sizeof(struct aligned64_vla_parameter_cell));
    for (int column = 0; column < source_columns; column++) {
      input[row][column].value =
          1000ULL + (unsigned long long)row * 101ULL +
          (unsigned long long)column * 13ULL;
      input[row][column].tag =
          (unsigned int)(row * 7 + column + 3);
      output[row][column].value = 0;
      output[row][column].tag = 0;
      expected += input[row][column].value +
                  (unsigned long long)row * 17ULL +
                  (unsigned long long)column * 5ULL;
      expected += input[row][column].tag +
                  (unsigned int)(row + column + 1);
    }
  }

  rows = source_rows + 19;
  columns = source_columns + 23;
  assert(transform_overaligned_vla_parameter(
             source_rows, source_columns, input, output) == expected);
  verify_transformed_matrix(
      source_rows, source_columns, input, output);

  for (int row = 0; row < source_rows; row++) {
    for (int column = 0; column < source_columns; column++) {
      output[row][column].value = 0;
      output[row][column].tag = 0;
    }
  }
  matrix_transform_t *indirect =
      transform_overaligned_vla_parameter;
  assert(indirect(source_rows, source_columns, input, output) ==
         expected);
  verify_transformed_matrix(
      source_rows, source_columns, input, output);

  assert(before == 0x1122334455667788ULL);
  assert(after == 0x8877665544332211ULL);
}

int main(void) {
  exercise_cross_tu_vla_parameter(2, 3);
  exercise_cross_tu_vla_parameter(5, 4);
  exercise_cross_tu_vla_parameter(3, 9);
  return 0;
}
