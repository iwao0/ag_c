/* Exercise record layout without assuming whether the target pointer is 4 or
 * 8 bytes. Member offsets must follow the active target DataLayout. */
#include <assert.h>
#include <stddef.h>

struct Cell {
  char lead;
  void *payload;
  short code;
};

struct Grid {
  char prefix;
  struct Cell cells[2];
  int (*apply)(int);
};

static int increment(int value) { return value + 1; }

int main(void) {
  int first = 10;
  int second = 20;
  struct Grid grid = {0};

  grid.cells[0].payload = &first;
  grid.cells[0].code = 3;
  grid.cells[1].payload = &second;
  grid.cells[1].code = 4;
  grid.apply = increment;

  assert(sizeof(grid.cells) == 2 * sizeof(struct Cell));
  assert((char *)&grid.cells[1] - (char *)&grid.cells[0] ==
         (ptrdiff_t)sizeof(struct Cell));
  assert(_Alignof(struct Cell) == _Alignof(void *));
  assert(((size_t)((char *)&grid.cells[0].payload -
                   (char *)&grid.cells[0])) %
          _Alignof(void *) == 0);
  assert(((size_t)((char *)&grid.apply - (char *)&grid)) %
          _Alignof(int (*)(int)) == 0);
  assert(sizeof(struct Grid) % _Alignof(struct Grid) == 0);
  assert(*(int *)grid.cells[0].payload + *(int *)grid.cells[1].payload == 30);
  assert(grid.apply(grid.cells[0].code + grid.cells[1].code) == 8);
  return 0;
}
