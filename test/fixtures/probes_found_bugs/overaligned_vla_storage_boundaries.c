// VLA allocation must honor both an explicit object alignment and the natural
// extended alignment of its element type at runtime.
// Expected: exit=0
#include <assert.h>
#include <stdint.h>

struct aligned32_cell {
  _Alignas(32) long value;
  unsigned char tag;
};

_Static_assert(_Alignof(struct aligned32_cell) == 32,
               "VLA element alignment");
_Static_assert(sizeof(struct aligned32_cell) == 32,
               "VLA element stride");

static int is_aligned(const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static long check_cells(int count,
                        struct aligned32_cell cells[static count]) {
  assert(is_aligned(cells, _Alignof(struct aligned32_cell)));
  long sum = 0;
  for (int index = 0; index < count; index++) {
    assert(is_aligned(&cells[index], _Alignof(struct aligned32_cell)));
    assert(cells[index].value == index * 17 + count);
    assert(cells[index].tag == (unsigned char)(index + 3));
    sum += cells[index].value + cells[index].tag;
  }
  return sum;
}

static long check_matrix(
    int rows, int columns,
    struct aligned32_cell matrix[rows][columns]) {
  assert(is_aligned(matrix, _Alignof(struct aligned32_cell)));
  long sum = 0;
  for (int row = 0; row < rows; row++) {
    assert(is_aligned(matrix[row], _Alignof(struct aligned32_cell)));
    assert((uintptr_t)&matrix[row][0] - (uintptr_t)&matrix[0][0] ==
           (uintptr_t)row * (uintptr_t)columns *
               sizeof(struct aligned32_cell));
    for (int column = 0; column < columns; column++) {
      struct aligned32_cell *cell = &matrix[row][column];
      assert(is_aligned(cell, _Alignof(struct aligned32_cell)));
      assert(cell->value == row * 100 + column * 7);
      assert(cell->tag == (unsigned char)(row + column + 11));
      sum += cell->value + cell->tag;
    }
  }
  return sum;
}

static long exercise_vlas(int count, int rows, int columns) {
  unsigned long before = 0x1122334455667788UL;
  _Alignas(64) struct aligned32_cell cells[count];
  struct aligned32_cell matrix[rows][columns];
  _Alignas(128) unsigned char bytes[count * 3 + 1];
  unsigned long after = 0x8877665544332211UL;

  assert(is_aligned(cells, 64));
  assert(is_aligned(matrix, _Alignof(struct aligned32_cell)));
  assert(is_aligned(bytes, 128));
  assert(sizeof(cells) == (unsigned long)count *
                              sizeof(struct aligned32_cell));
  assert(sizeof(matrix) ==
         (unsigned long)rows * (unsigned long)columns *
             sizeof(struct aligned32_cell));
  assert(sizeof(bytes) == (unsigned long)(count * 3 + 1));

  for (int index = 0; index < count; index++) {
    cells[index].value = index * 17 + count;
    cells[index].tag = (unsigned char)(index + 3);
  }
  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < columns; column++) {
      matrix[row][column].value = row * 100 + column * 7;
      matrix[row][column].tag = (unsigned char)(row + column + 11);
    }
  }
  for (int index = 0; index < count * 3 + 1; index++)
    bytes[index] = (unsigned char)(index * 5 + 1);

  long sum = check_cells(count, cells) +
             check_matrix(rows, columns, matrix);
  for (int index = 0; index < count * 3 + 1; index++)
    sum += bytes[index];

  {
    _Alignas(64) unsigned long long nested[count + 1];
    assert(is_aligned(nested, 64));
    for (int index = 0; index < count + 1; index++) {
      nested[index] = 0x100000000ULL + (unsigned long long)index;
      sum += (long)(nested[index] - 0x100000000ULL);
    }
  }

  assert(before == 0x1122334455667788UL);
  assert(after == 0x8877665544332211UL);
  return sum;
}

int main(void) {
  long first = exercise_vlas(3, 2, 3);
  long second = exercise_vlas(7, 3, 2);
  long third = exercise_vlas(11, 4, 3);
  assert(first == 730);
  assert(second == 2349);
  assert(third == 6095);
  return 0;
}
