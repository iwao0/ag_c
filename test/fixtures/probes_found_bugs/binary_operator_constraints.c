#include <assert.h>
#include <complex.h>

static int identity(int value) {
  return value;
}

int main(void) {
  int values[3] = {4, 5, 6};
  int *pointer = values;
  const int *qualified_pointer = values;
  void *void_pointer = values;
  int (*function_pointer)(int) = identity;
  double scalar = 1.5;
  double _Complex first_complex = 1.0 + 2.0 * I;
  double _Complex second_complex = 1.0 + 2.0 * I;
  int length = 3;
  int matrix[2][length];
  int (*row)[length] = matrix;

  assert(pointer != 0);
  assert(0 != pointer);
  assert(pointer == qualified_pointer);
  assert(pointer == void_pointer);
  assert(function_pointer == identity);
  assert(&values[0] < &values[2]);
  assert(pointer + 2 == &values[2]);
  assert(2 + pointer == &values[2]);
  assert(&values[2] - 2 == pointer);
  assert(&values[2] - pointer == 2);
  assert(row + 1 == &matrix[1]);
  assert((5 % 3) == 2);
  assert((5 & 3) == 1);
  assert((1 << 3) == 8);
  assert((scalar && pointer) == 1);
  assert(first_complex == second_complex);
  return 0;
}
