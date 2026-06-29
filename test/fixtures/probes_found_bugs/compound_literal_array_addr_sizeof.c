#include <assert.h>

int main(void) {
  int (*pi)[3] = &(int[3]){1, 2, 3};
  assert(sizeof(&(int[3]){1, 2, 3}) == 8);
  assert((*pi)[0] == 1);
  assert((*pi)[2] == 3);

  long (*pl)[2] = &(long[2]){4, 5};
  assert(sizeof(&(long[2]){4, 5}) == 8);
  assert((*pl)[0] == 4);
  assert((*pl)[1] == 5);
  return 0;
}
