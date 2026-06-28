/* static local multidimensional integer arrays must lower to static storage.
 * Before this fix, the consumed array suffix path only handled 1D arrays, so
 * `static int a[2][3]` fell back to an auto stack array and lost persistence
 * between calls. */
#include <assert.h>

int bump_2d(void) {
  static int a[2][3];
  a[1][2] = a[1][2] + 1;
  return a[1][2];
}

int read_init_2d(void) {
  static int m[2][3] = {{1, 2, 3}, {4, 5, 6}};
  m[0][1] = m[0][1] + 10;
  return m[0][1] + m[1][2];
}

int bump_3d(void) {
  static int cube[2][2][2] = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
  cube[1][0][1] = cube[1][0][1] + 1;
  return cube[1][0][1] + cube[0][1][1];
}

int main(void) {
  assert(bump_2d() == 1);
  assert(bump_2d() == 2);
  assert(read_init_2d() == 18);
  assert(read_init_2d() == 28);
  assert(bump_3d() == 11);
  assert(bump_3d() == 12);
  static int sized[2][3];
  assert(sizeof(sized) == 24);
  assert(sizeof(sized[0]) == 12);
  return 0;
}
