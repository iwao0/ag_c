#include <assert.h>

struct S {
  int x;
  int y;
};

int main(void) {
  int (*pi)[3] = &(int[3]){1, 2, 3};
  assert(sizeof(&(int[3]){1, 2, 3}) == 8);
  assert(sizeof(*(&(int[3]){1, 2, 3})) == 12);
  assert((*pi)[0] == 1);
  assert((*pi)[2] == 3);
  assert((&(int[3]){4, 5, 6})[0][2] == 6);

  long (*pl)[2] = &(long[2]){4, 5};
  assert(sizeof(&(long[2]){4, 5}) == 8);
  assert((*pl)[0] == 4);
  assert((*pl)[1] == 5);

  struct S (*ps)[2] = &(struct S[2]){{1, 2}, {3, 4}};
  assert(sizeof(*ps) == 16);
  assert((*ps)[0].y == 2);
  assert((*ps)[1].x == 3);
  assert((&(struct S[2]){{5, 6}, {7, 8}})[0][1].y == 8);
  return 0;
}
