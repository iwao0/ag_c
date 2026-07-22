/* Address-of on a VLA subarray must retain the runtime row bound and stride. */
#include <assert.h>

static int read_last(int columns, int (*rows)[columns], int index) {
  int (*row)[columns] = &rows[index];
  return (*row)[columns - 1];
}

static void write_first(int columns, int (*rows)[columns], int index,
                        int value) {
  int (*row)[columns] = &rows[index];
  (*row)[0] = value;
}

int main(void) {
  int columns = 4;
  int rows[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
  assert(read_last(columns, rows, 0) == 4);
  assert(read_last(columns, rows, 1) == 8);
  write_first(columns, rows, 1, 12);
  assert(rows[1][0] == 12);
  return 0;
}
