#include <assert.h>

static int sum_values(int count, int values[const *]);
static int sum_row(int rows, int matrix[restrict *][4]);

static int sum_values(int count, int *const values) {
  int sum = 0;
  for (int i = 0; i < count; i++) sum += values[i];
  return sum;
}

static int sum_row(int rows, int (*restrict matrix)[4]) {
  int sum = 0;
  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < 4; column++) sum += matrix[row][column];
  }
  return sum;
}

int main(void) {
  int values[3] = {10, 12, 20};
  int matrix[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 14}};
  assert(sum_values(3, values) == 42);
  assert(sum_row(2, matrix) == 42);
  return 0;
}
