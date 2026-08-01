// Variably modified array typedefs must preserve captured bounds, element
// stride, and extended alignment when objects are allocated from the alias.
// Expected: exit=0.
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

struct aligned64_typedef_cell {
  _Alignas(64) unsigned long long value;
  unsigned int tag;
};

_Static_assert(_Alignof(struct aligned64_typedef_cell) == 64,
               "typedef VLA element alignment");
_Static_assert(sizeof(struct aligned64_typedef_cell) == 64,
               "typedef VLA element stride");

static int capture_typedef_bound(int *evaluations, int bound) {
  *evaluations += 1;
  return bound;
}

static int is_aligned_typedef_vla(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static unsigned long long exercise_typedef_vlas(
    int source_extent, int source_rows) {
  int evaluations = 0;
  int extent = source_extent;
  int rows = source_rows;
  typedef struct aligned64_typedef_cell CapturedRow[
      capture_typedef_bound(&evaluations, extent++)];
  typedef CapturedRow CapturedMatrix[
      capture_typedef_bound(&evaluations, rows++)];

  assert(evaluations == 2);
  assert(extent == source_extent + 1);
  assert(rows == source_rows + 1);

  unsigned long long before = 0x1122334455667788ULL;
  CapturedRow first;
  _Alignas(128) CapturedRow explicit_row;
  CapturedMatrix matrix;
  unsigned long long after = 0x8877665544332211ULL;

  extent = source_extent + 17;
  rows = source_rows + 19;
  assert(evaluations == 2);
  assert(sizeof(first) ==
         (size_t)source_extent * sizeof(struct aligned64_typedef_cell));
  assert(sizeof(explicit_row) == sizeof(first));
  assert(sizeof(matrix) ==
         (size_t)source_rows * (size_t)source_extent *
             sizeof(struct aligned64_typedef_cell));
  assert(is_aligned_typedef_vla(first, 64));
  assert(is_aligned_typedef_vla(explicit_row, 128));
  assert(is_aligned_typedef_vla(matrix, 64));

  unsigned long long total = 0;
  for (int column = 0; column < source_extent; column++) {
    assert(is_aligned_typedef_vla(&first[column], 64));
    assert(is_aligned_typedef_vla(&explicit_row[column], 64));
    first[column].value =
        100ULL + (unsigned long long)column * 3ULL;
    first[column].tag = (unsigned int)(column + 5);
    explicit_row[column].value =
        200ULL + (unsigned long long)column * 7ULL;
    explicit_row[column].tag = (unsigned int)(column + 11);
    total += first[column].value + first[column].tag;
    total += explicit_row[column].value + explicit_row[column].tag;
  }

  for (int row = 0; row < source_rows; row++) {
    assert(is_aligned_typedef_vla(matrix[row], 64));
    assert((uintptr_t)&matrix[row][0] - (uintptr_t)&matrix[0][0] ==
           (uintptr_t)row * (uintptr_t)source_extent *
               sizeof(struct aligned64_typedef_cell));
    for (int column = 0; column < source_extent; column++) {
      assert(is_aligned_typedef_vla(&matrix[row][column], 64));
      matrix[row][column].value =
          1000ULL + (unsigned long long)row * 100ULL +
          (unsigned long long)column * 13ULL;
      matrix[row][column].tag =
          (unsigned int)(row + column + 17);
      total += matrix[row][column].value + matrix[row][column].tag;
    }
  }

  assert(before == 0x1122334455667788ULL);
  assert(after == 0x8877665544332211ULL);
  return total;
}

int main(void) {
  unsigned long long first = exercise_typedef_vlas(3, 2);
  unsigned long long second = exercise_typedef_vlas(7, 4);
  unsigned long long third = exercise_typedef_vlas(11, 3);
  assert(first != 0);
  assert(second > first);
  assert(third > second);
  return 0;
}
