#include <assert.h>

static int update_selected_row(void) {
  static int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
  static int (*selected_row)[3] = &grid[1];

  (*selected_row)[0] += 10;
  return grid[1][0] + (*selected_row)[2];
}

int main(void) {
  assert(update_selected_row() == 20);
  assert(update_selected_row() == 30);
  return 0;
}
