// c-testsuite 00219: _Generic must not treat an array association type as
// the element scalar type, and a function designator decays to a function
// pointer so it can match a function-pointer typedef.
#include <assert.h>

typedef int (*unary_fn)(int);
typedef int int_vec4[4];

int id(int x) {
  return x;
}

int main(void) {
  int x = 0;
  int_vec4 v = {1, 2, 3, 4};

  assert(_Generic(x, char: 1, int[4]: 2, default: 5) == 5);
  assert(_Generic(x, int_vec4: 2, int: 3, default: 5) == 3);
  assert(_Generic(v, int *: 7, int_vec4: 2, default: 5) == 7);
  assert(_Generic(id, unary_fn: 9, int: 4, default: 5) == 9);
  return 0;
}
