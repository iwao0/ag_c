#include <assert.h>

struct Cell {
  int id;
  int *p;
};

int pool[4] = {10, 20, 30, 40};

struct Cell grid[2][2] = {
    {{0, 0}, {1, &pool[1]}},
    {{2, &pool[2]}, {0, 0}},
};

int main(void) {
  assert(grid[0][0].id == 0);
  assert(grid[0][0].p == 0);
  assert(grid[0][1].id == 1);
  assert(*grid[0][1].p == 20);
  assert(grid[1][0].id == 2);
  assert(grid[1][0].p[1] == 40);
  grid[1][0].p[1] += 2;
  assert(pool[3] == 42);
  return 0;
}
