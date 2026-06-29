#include <assert.h>

int values[4] = {3, 5, 7, 11};

int bump_static_ptrs(void) {
  static int *ptrs[3] = {&values[0], &values[2], &values[3]};
  *ptrs[0] += 1;
  *ptrs[1] += *ptrs[0];
  return *ptrs[1] + *ptrs[2];
}

int main(void) {
  assert(bump_static_ptrs() == 22);
  assert(values[0] == 4);
  assert(values[2] == 11);
  assert(bump_static_ptrs() == 27);
  assert(values[0] == 5);
  assert(values[2] == 16);
  assert(values[3] == 11);
  return 0;
}
