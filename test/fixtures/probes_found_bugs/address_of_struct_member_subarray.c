/* Preserve a member row's array type through ->, subscript, and address-of. */
#include <assert.h>

struct Matrix {
  int rows[2][3];
};

static int (*matrix_row(struct Matrix *matrix, int index))[3] {
  return &matrix->rows[index];
}

int main(void) {
  struct Matrix matrix = {{{1, 2, 3}, {4, 5, 6}}};
  assert((*matrix_row(&matrix, 0))[2] == 3);
  assert((*matrix_row(&matrix, 1))[0] == 4);
  (*matrix_row(&matrix, 1))[1] = 10;
  assert(matrix.rows[1][1] == 10);
  return 0;
}
