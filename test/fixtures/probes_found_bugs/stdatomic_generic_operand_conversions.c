// stdatomic generic-function operand conversions.
// Expected: exit=0
#include <complex.h>
#include <stdatomic.h>

static int value_evaluations;

static double next_double(double value) {
  value_evaluations++;
  return value;
}

static double complex next_complex(
    double real, double imaginary_part) {
  value_evaluations++;
  return real + imaginary_part * I;
}

int main(void) {
  atomic_int integer = ATOMIC_VAR_INIT(1);
  atomic_store(&integer, next_double(3.75));
  if (atomic_load(&integer) != 3 ||
      atomic_exchange(&integer, next_double(4.75)) != 3)
    return 1;

  int expected = 4;
  if (!atomic_compare_exchange_strong(
          &integer, &expected, next_double(5.75)) ||
      atomic_fetch_add(
          &integer, next_complex(2.75, 9.0)) != 5 ||
      atomic_fetch_sub(&integer, next_double(1.75)) != 7 ||
      atomic_fetch_or(&integer, next_double(8.75)) != 6 ||
      atomic_fetch_xor(&integer, next_double(3.75)) != 14 ||
      atomic_fetch_and(&integer, next_double(7.75)) != 13 ||
      atomic_load(&integer) != 5)
    return 2;

  _Atomic(float) floating = ATOMIC_VAR_INIT(0.0f);
  atomic_store(
      &floating, next_complex(3.5, 4.5));
  if (atomic_load(&floating) != 3.5f ||
      atomic_exchange(&floating, 6) != 3.5f)
    return 3;
  float expected_float = 6.0f;
  if (!atomic_compare_exchange_strong(
          &floating, &expected_float, 7) ||
      atomic_load(&floating) != 7.0f)
    return 4;

  _Atomic(double complex) complex_value =
      ATOMIC_VAR_INIT(1.0 + 2.0 * I);
  atomic_store(&complex_value, 9);
  double complex previous_complex =
      atomic_exchange(&complex_value, 10.5f);
  double complex expected_complex = 10.5;
  if (creal(previous_complex) != 9.0 ||
      cimag(previous_complex) != 0.0 ||
      !atomic_compare_exchange_strong(
          &complex_value, &expected_complex, 11) ||
      creal(atomic_load(&complex_value)) != 11.0 ||
      cimag(atomic_load(&complex_value)) != 0.0)
    return 5;

  int values[8] = {0};
  _Atomic(int *) pointer = ATOMIC_VAR_INIT(values);
  atomic_store(&pointer, 0);
  if (atomic_exchange(&pointer, values) != 0)
    return 6;
  int *expected_pointer = values;
  if (!atomic_compare_exchange_strong(
          &pointer, &expected_pointer, 0) ||
      atomic_load(&pointer) != 0)
    return 7;

  atomic_store(&pointer, values);
  if (atomic_fetch_add(
          &pointer, next_double(2.75)) != values ||
      atomic_load(&pointer) != values + 2 ||
      atomic_fetch_sub(
          &pointer, next_complex(1.75, 6.0)) != values + 2 ||
      atomic_load(&pointer) != values + 1)
    return 8;

  if (value_evaluations != 11 ||
      !_Generic(
          atomic_fetch_add(&integer, 0.0),
          int: 1,
          default: 0) ||
      !_Generic(
          atomic_fetch_add(&pointer, 0.0),
          int *: 1,
          default: 0))
    return 9;
  return 0;
}
