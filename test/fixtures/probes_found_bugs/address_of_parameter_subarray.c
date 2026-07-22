/* A multidimensional array parameter is adjusted to a pointer to its row.
 * Taking the address of a selected row must preserve the row array type. */
#include <assert.h>

static int (*row_at(int rows[][3], int index))[3] {
  return &rows[index];
}

int main(void) {
  int rows[2][3] = {{1, 2, 3}, {4, 5, 6}};
  int (*second)[3] = row_at(rows, 1);
  assert((*second)[0] == 4);
  assert((*second)[2] == 6);
  (*row_at(rows, 0))[1] = 9;
  assert(rows[0][1] == 9);
  return 0;
}
