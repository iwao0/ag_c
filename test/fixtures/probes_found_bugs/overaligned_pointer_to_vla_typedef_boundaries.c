// A pointer formed from a runtime-bound array typedef must retain the
// typedef's captured row stride and the element's extended alignment.
// Expected: exit=0.
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

struct aligned64_pointer_vla_cell {
  _Alignas(64) unsigned long long value;
  unsigned int tag;
};

_Static_assert(_Alignof(struct aligned64_pointer_vla_cell) == 64,
               "pointer-to-VLA element alignment");
_Static_assert(sizeof(struct aligned64_pointer_vla_cell) == 64,
               "pointer-to-VLA element stride");

typedef unsigned long long (*pointer_vla_inspector_t)(
    int rows, int columns,
    struct aligned64_pointer_vla_cell (*matrix)[*]);

static int capture_pointer_vla_bound(
    int *evaluations, int bound) {
  *evaluations += 1;
  return bound;
}

static int is_pointer_vla_aligned(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static unsigned long long inspect_pointer_vla_parameter(
    int rows, int columns,
    struct aligned64_pointer_vla_cell
        (*matrix)[columns > 0 ? columns : 1]) {
  struct aligned64_pointer_vla_cell
      (*begin)[columns > 0 ? columns : 1] = matrix;
  struct aligned64_pointer_vla_cell
      (*cursor)[columns > 0 ? columns : 1] = matrix;

  assert(sizeof *matrix ==
         (size_t)columns *
             sizeof(struct aligned64_pointer_vla_cell));
  assert(is_pointer_vla_aligned(matrix, 64));
  cursor += rows;
  assert((uintptr_t)cursor - (uintptr_t)begin ==
         (uintptr_t)rows * (uintptr_t)columns *
             sizeof(struct aligned64_pointer_vla_cell));
  cursor--;
  assert(cursor == begin + rows - 1);
  ++cursor;
  assert(cursor == begin + rows);
  --cursor;

  unsigned long long total = 0;
  for (int row = 0; row < rows; row++) {
    assert(is_pointer_vla_aligned(begin + row, 64));
    assert((uintptr_t)(begin + row) - (uintptr_t)begin ==
           (uintptr_t)row * (uintptr_t)columns *
               sizeof(struct aligned64_pointer_vla_cell));
    for (int column = 0; column < columns; column++) {
      assert(is_pointer_vla_aligned(&begin[row][column], 64));
      total += begin[row][column].value;
      total += begin[row][column].tag;
    }
  }
  cursor[0][columns - 1].tag += 7U;
  return total;
}

static void exercise_pointer_vla_typedefs(
    int source_rows, int source_columns) {
  int evaluations = 0;
  int columns = source_columns;
  int captured_columns =
      capture_pointer_vla_bound(&evaluations, columns++);
  typedef struct aligned64_pointer_vla_cell CapturedRow[
      captured_columns > 0 ? captured_columns : 1];
  typedef CapturedRow *CapturedRowPointer;
  typedef CapturedRowPointer PointerAlias;

  assert(evaluations == 1);
  assert(columns == source_columns + 1);

  int rows = source_rows;
  _Alignas(128) CapturedRow matrix[rows];
  CapturedRowPointer begin = matrix;
  PointerAlias cursor = begin;
  CapturedRowPointer *handle = &begin;

  columns = source_columns + 23;
  captured_columns = source_columns + 31;
  rows = source_rows + 29;
  assert(evaluations == 1);
  assert(sizeof *begin ==
         (size_t)source_columns *
             sizeof(struct aligned64_pointer_vla_cell));
  assert(sizeof **handle == sizeof *begin);
  assert(sizeof matrix ==
         (size_t)source_rows * sizeof *begin);
  assert(is_pointer_vla_aligned(matrix, 128));

  unsigned long long expected = 0;
  for (int row = 0; row < source_rows; row++) {
    assert(is_pointer_vla_aligned(cursor, 64));
    assert((uintptr_t)cursor - (uintptr_t)begin ==
           (uintptr_t)row * sizeof *begin);
    for (int column = 0; column < source_columns; column++) {
      assert(is_pointer_vla_aligned(&(*cursor)[column], 64));
      (*cursor)[column].value =
          1000ULL + (unsigned long long)row * 101ULL +
          (unsigned long long)column * 13ULL;
      (*cursor)[column].tag =
          (unsigned int)(row * 7 + column + 3);
      expected += (*cursor)[column].value;
      expected += (*cursor)[column].tag;
    }
    cursor++;
  }

  assert(cursor == begin + source_rows);
  assert(cursor-- == begin + source_rows);
  assert(cursor == begin + source_rows - 1);
  assert(++cursor == begin + source_rows);
  cursor -= source_rows;
  assert(cursor == begin);
  assert((*handle)[source_rows - 1][source_columns - 1].value ==
         1000ULL +
             (unsigned long long)(source_rows - 1) * 101ULL +
             (unsigned long long)(source_columns - 1) * 13ULL);

  unsigned int original_last_tag =
      (unsigned int)((source_rows - 1) * 7 +
                     (source_columns - 1) + 3);
  assert(inspect_pointer_vla_parameter(
             source_rows, source_columns, matrix) == expected);
  assert(matrix[source_rows - 1][source_columns - 1].tag ==
         original_last_tag + 7U);

  matrix[source_rows - 1][source_columns - 1].tag =
      original_last_tag;
  pointer_vla_inspector_t indirect =
      inspect_pointer_vla_parameter;
  assert(indirect(source_rows, source_columns, matrix) == expected);
  assert(matrix[source_rows - 1][source_columns - 1].tag ==
         original_last_tag + 7U);
}

int main(void) {
  exercise_pointer_vla_typedefs(2, 3);
  exercise_pointer_vla_typedefs(5, 7);
  exercise_pointer_vla_typedefs(3, 11);
  return 0;
}
