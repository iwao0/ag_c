#include <assert.h>

int sum4(int *p) {
  return p[0] + p[1] + p[2] + p[3];
}

int main(void) {
  assert(sizeof((int[]){1, 2, 3, 4}) == 16);
  assert(sum4((int[]){4, 5, 6, 7}) == 22);

  assert(sizeof((char[]){"hello"}) == 6);
  assert(sizeof((char[]){"ab" "cd"}) == 5);
  char *s = (char[]){"xy"};
  assert(sizeof((char[]){'a', 'b', 'c', 0}) == 4);
  assert(s[0] == 'x');
  assert(s[2] == 0);
  return 0;
}
