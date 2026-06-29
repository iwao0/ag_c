/* A typedef-array static local must persist like a directly declared
 * multidimensional static local array. The old path registered it as an
 * automatic typedef-array local, so each call reinitialized the storage. */
#include <assert.h>

typedef int I2x3[2][3];
typedef unsigned char U2x3[2][3];
typedef double D2x2[2][2];
typedef int *IP2x2[2][2];

int pointer_values[4] = {10, 20, 30, 40};

int bump_typedef_int_2d(void) {
  static I2x3 a = {{1, 2, 3}, {4, 5, 6}};
  a[1][2] += 1;
  return a[1][2] + a[0][1];
}

int bump_typedef_unsigned_char_2d(void) {
  static U2x3 a = {{1, 2, 3}, {4, 5, 250}};
  a[1][2] += 1;
  return a[1][2];
}

int bump_typedef_double_2d(void) {
  static D2x2 a = {{1.5, 2.5}, {3.5, 4.5}};
  a[1][0] += 1.0;
  return (int)(a[1][0] + a[0][1]);
}

int bump_typedef_pointer_2d(void) {
  static IP2x2 ptrs = {
      {&pointer_values[0], &pointer_values[1]},
      {&pointer_values[2], &pointer_values[3]}};
  *ptrs[1][0] += *ptrs[0][1];
  *ptrs[0][0] += 1;
  return *ptrs[1][0] + *ptrs[0][0];
}

int main(void) {
  assert(bump_typedef_int_2d() == 9);
  assert(bump_typedef_int_2d() == 10);
  assert(bump_typedef_unsigned_char_2d() == 251);
  assert(bump_typedef_unsigned_char_2d() == 252);
  assert(bump_typedef_double_2d() == 7);
  assert(bump_typedef_double_2d() == 8);
  assert(bump_typedef_pointer_2d() == 61);
  assert(pointer_values[0] == 11);
  assert(pointer_values[2] == 50);
  assert(bump_typedef_pointer_2d() == 82);
  assert(pointer_values[0] == 12);
  assert(pointer_values[2] == 70);
  return 0;
}
