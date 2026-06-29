#include <assert.h>

int values[4] = {3, 5, 7, 11};
int direct_a = 1, direct_b = 2, direct_c = 3, direct_d = 4;

int bump_static_ptrs(void) {
  static int *ptrs[3] = {&values[0], &values[2], &values[3]};
  *ptrs[0] += 1;
  *ptrs[1] += *ptrs[0];
  return *ptrs[1] + *ptrs[2];
}

int rotate_direct_static_ptrs(void) {
  static int *ptrs[2][2] = {{&direct_a, &direct_b}, {&direct_c, &direct_d}};
  int *old = ptrs[0][0];
  ptrs[0][0] = ptrs[1][1];
  ptrs[1][1] = old;
  return *ptrs[0][0] * 10 + *ptrs[1][1];
}

int main(void) {
  assert(bump_static_ptrs() == 22);
  assert(values[0] == 4);
  assert(values[2] == 11);
  assert(bump_static_ptrs() == 27);
  assert(values[0] == 5);
  assert(values[2] == 16);
  assert(values[3] == 11);
  assert(rotate_direct_static_ptrs() == 41);
  assert(rotate_direct_static_ptrs() == 14);
  return 0;
}
