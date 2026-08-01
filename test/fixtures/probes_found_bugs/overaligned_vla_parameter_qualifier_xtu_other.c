// Definition side of overaligned_vla_parameter_qualifier_xtu_main.c.
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

static int is_other_matrix_aligned(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

unsigned long long transform_overaligned_vla_parameter(
    int rows, int columns,
    const struct aligned64_vla_parameter_cell
        (*restrict input)[columns],
    struct aligned64_vla_parameter_cell
        (*restrict output)[columns]) {
  assert(sizeof *input ==
         (size_t)columns *
             sizeof(struct aligned64_vla_parameter_cell));
  assert(sizeof *output == sizeof *input);
  assert(is_other_matrix_aligned(input, 64));
  assert(is_other_matrix_aligned(output, 64));

  unsigned long long total = 0;
  for (int row = 0; row < rows; row++) {
    assert(is_other_matrix_aligned(input + row, 64));
    assert(is_other_matrix_aligned(output + row, 64));
    assert((uintptr_t)(input + row) - (uintptr_t)input ==
           (uintptr_t)row * (uintptr_t)columns *
               sizeof(struct aligned64_vla_parameter_cell));
    assert((uintptr_t)(output + row) - (uintptr_t)output ==
           (uintptr_t)row * (uintptr_t)columns *
               sizeof(struct aligned64_vla_parameter_cell));
    for (int column = 0; column < columns; column++) {
      assert(is_other_matrix_aligned(&input[row][column], 64));
      assert(is_other_matrix_aligned(&output[row][column], 64));
      output[row][column].value =
          input[row][column].value +
          (unsigned long long)row * 17ULL +
          (unsigned long long)column * 5ULL;
      output[row][column].tag =
          input[row][column].tag +
          (unsigned int)(row + column + 1);
      total += output[row][column].value;
      total += output[row][column].tag;
    }
  }
  return total;
}
