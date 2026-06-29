#include <assert.h>

int *gp = (int[]){7, 8, 9};
int *gq = &(int[]){10, 11, 12}[1];

struct Holder {
  int *p;
  int len;
};

struct Holder holders[2] = {
    {.p = (int[]){1, 2, 3}, .len = 3},
    {.p = &(int[]){4, 5, 6}[2], .len = 1},
};

int main(void) {
  assert(gp[0] == 7);
  assert(gp[2] == 9);
  assert(*gq == 11);
  assert(holders[0].p[1] == 2);
  assert(*holders[1].p == 6);
  holders[0].p[2] += holders[1].len;
  assert(gp[2] == 9);
  assert(holders[0].p[2] == 4);
  return 0;
}
