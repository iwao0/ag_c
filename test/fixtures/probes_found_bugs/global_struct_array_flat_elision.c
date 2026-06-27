// Global struct arrays with an incomplete outer dimension must infer the
// element count from flattened struct slots, not from the raw scalar count.
#include <assert.h>

struct PT {
  long c[4];
  long b;
  long e;
  long k;
};

struct Pair {
  int x;
  int y;
};

struct PT cases[] = {
  1, 2, 3, 4, 5, 6, 7,
  8, 9, 10, 11, 12, 13, 14,
};

struct Pair partial[] = {1, 2, 3};
struct Pair braced[] = {{1}, {2, 3}};

int main(void) {
  assert(sizeof(cases) / sizeof(cases[0]) == 2);
  assert(cases[0].c[0] == 1);
  assert(cases[0].c[1] == 2);
  assert(cases[0].c[2] == 3);
  assert(cases[0].c[3] == 4);
  assert(cases[0].b == 5);
  assert(cases[0].e == 6);
  assert(cases[0].k == 7);
  assert(cases[1].c[0] == 8);
  assert(cases[1].c[1] == 9);
  assert(cases[1].c[2] == 10);
  assert(cases[1].c[3] == 11);
  assert(cases[1].b == 12);
  assert(cases[1].e == 13);
  assert(cases[1].k == 14);

  assert(sizeof(partial) / sizeof(partial[0]) == 2);
  assert(partial[0].x == 1);
  assert(partial[0].y == 2);
  assert(partial[1].x == 3);
  assert(partial[1].y == 0);

  assert(sizeof(braced) / sizeof(braced[0]) == 2);
  assert(braced[0].x == 1);
  assert(braced[0].y == 0);
  assert(braced[1].x == 2);
  assert(braced[1].y == 3);
  return 0;
}
