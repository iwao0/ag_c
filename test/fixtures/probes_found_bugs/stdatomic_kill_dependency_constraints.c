// stdatomic kill_dependency scalar constraint and value category.
// Expected: exit=0
#include <complex.h>
#include <stdatomic.h>

static int evaluations;

static int next_integer(int value) {
  evaluations++;
  return value;
}

static double complex next_complex(
    double real, double imaginary_part) {
  evaluations++;
  return real + imaginary_part * I;
}

static int *next_pointer(int *value) {
  evaluations++;
  return value;
}

static int plus_one(int value) {
  return value + 1;
}

int main(void) {
  const int constant = 7;
  atomic_int atomic_value = ATOMIC_VAR_INIT(9);
  int values[2] = {11, 13};

  if (kill_dependency(next_integer(5)) != 5 ||
      kill_dependency(constant) != 7 ||
      kill_dependency(atomic_value) != 9)
    return 1;

  double complex complex_value =
      kill_dependency(next_complex(3.5, 4.5));
  if (creal(complex_value) != 3.5 ||
      cimag(complex_value) != 4.5)
    return 2;

  int *pointer = kill_dependency(next_pointer(values));
  int *array_pointer = kill_dependency(values);
  int (*function_pointer)(int) =
      kill_dependency(plus_one);
  if (pointer != values || array_pointer != values ||
      function_pointer(17) != 18)
    return 3;

  if (evaluations != 3 ||
      !_Generic(
          kill_dependency(constant),
          int: 1,
          default: 0) ||
      !_Generic(
          kill_dependency(atomic_value),
          int: 1,
          default: 0) ||
      !_Generic(
          kill_dependency(complex_value),
          double complex: 1,
          default: 0) ||
      !_Generic(
          kill_dependency(values),
          int *: 1,
          default: 0) ||
      !_Generic(
          kill_dependency(plus_one),
          int (*)(int): 1,
          default: 0))
    return 4;
  return 0;
}
