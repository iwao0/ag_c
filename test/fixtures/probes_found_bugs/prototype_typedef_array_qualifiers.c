/* Array-parameter adjustment must produce the same canonical function type
 * when static and restrict appear in a different valid order. */
#include <assert.h>

typedef int Row[3];

int sum_rows(int count, const Row rows[static restrict 1]);

int sum_rows(int count, const Row rows[restrict static 1]) {
  int sum = 0;
  for (int row = 0; row < count; row++) {
    for (int column = 0; column < 3; column++) {
      sum += rows[row][column];
    }
  }
  return sum;
}

int main(void) {
  Row rows[2] = {{1, 2, 3}, {4, 5, 6}};
  assert(sum_rows(2, rows) == 21);
  return 0;
}
