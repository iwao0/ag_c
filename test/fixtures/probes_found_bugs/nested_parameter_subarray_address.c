#include <assert.h>

static int (*select_row(int cube[][2][3], int page, int row))[3] {
  return &cube[page][row];
}

int main(void) {
  int cube[2][2][3] = {
      {{1, 2, 3}, {4, 5, 6}},
      {{7, 8, 9}, {10, 11, 12}},
  };
  int (*selected_row)[3] = select_row(cube, 1, 0);

  assert((*selected_row)[0] == 7);
  assert((*selected_row)[2] == 9);
  (*select_row(cube, 0, 1))[1] = 50;
  assert(cube[0][1][1] == 50);
  return 0;
}
