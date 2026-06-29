#include <assert.h>

int read_second(int *p) {
  return p[1];
}

int main(void) {
  assert(sizeof((int[3]){1, 2, 3}) == 12);
  assert(sizeof((char[3]){'a', 'b', 0}) == 3);
  assert(read_second((int[3]){4, 5, 6}) == 5);

  int *p = (int[3]){7, 8, 9};
  assert(p[0] == 7);
  assert(p[2] == 9);

  struct S {
    int x;
    int y;
  };
  assert(sizeof((struct S[2]){{1, 2}, {3, 4}}) == 16);
  assert(((struct S[2]){{1, 2}, {3, 4}})[1].y == 4);

  return 0;
}
