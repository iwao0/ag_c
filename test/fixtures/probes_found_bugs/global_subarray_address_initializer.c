#include <assert.h>

static int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
static int (*selected_row)[3] = &grid[1];

int main(void) {
  assert((*selected_row)[0] == 4);
  assert((*selected_row)[2] == 6);

  (*selected_row)[1] = 50;
  assert(grid[1][1] == 50);
  assert(selected_row - grid == 1);
  return 0;
}
