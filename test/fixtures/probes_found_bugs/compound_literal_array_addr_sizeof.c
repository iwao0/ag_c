#include <assert.h>

struct S {
  int x;
  int y;
};

typedef int Row3[3];
typedef Row3 Mat2[2];
typedef struct S Pair[2];

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

  int (*pm)[2][3] = &(int[2][3]){{1, 2, 3}, {4, 5, 6}};
  assert(sizeof(&(int[2][3]){{1, 2, 3}, {4, 5, 6}}) == 8);
  assert(sizeof(*pm) == 24);
  assert((*pm)[0][1] == 2);
  assert((*pm)[1][2] == 6);
  assert((&(int[2][3]){{7, 8, 9}, {10, 11, 12}})[0][1][0] == 10);

  Row3 *rp = &(Row3){1, 2, 3};
  assert(sizeof(&(Row3){1, 2, 3}) == 8);
  assert(sizeof(*rp) == 12);
  assert((*rp)[2] == 3);
  assert((&(Row3){4, 5, 6})[0][1] == 5);

  Mat2 *mp = &(Mat2){{1, 2, 3}, {4, 5, 6}};
  assert(sizeof(&(Mat2){{1, 2, 3}, {4, 5, 6}}) == 8);
  assert(sizeof(*mp) == 24);
  assert((*mp)[1][2] == 6);
  assert((&(Mat2){{7, 8, 9}, {10, 11, 12}})[0][1][1] == 11);

  Pair *pp = &(Pair){{1, 2}, {3, 4}};
  assert(sizeof(*pp) == 16);
  assert((*pp)[1].y == 4);
  assert((&(Pair){{5, 6}, {7, 8}})[0][0].x == 5);
  return 0;
}
