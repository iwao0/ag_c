// 16-byte atomic complex compound-assignment boundaries.
// Expected: exit=0
#include <complex.h>

static _Atomic(double complex) global_value =
    CMPLX(1.25, -2.5);
static _Atomic(long double complex) global_long_value =
    CMPLXL(2.0L, 3.0L);
static _Atomic(double complex) selected_values[2];
static int lhs_evaluations;
static int rhs_evaluations;

static int is_double_complex(
    double complex value, double real, double imag) {
  return creal(value) == real && cimag(value) == imag;
}

static int is_long_complex(
    long double complex value,
    long double real, long double imag) {
  return creall(value) == real && cimagl(value) == imag;
}

static _Atomic(double complex) *selected_value(void) {
  lhs_evaluations++;
  return &selected_values[1];
}

static double complex next_increment(void) {
  rhs_evaluations++;
  return CMPLX(2.75, 0.5);
}

int main(void) {
  selected_values[1] = global_value;
  double complex result =
      (*selected_value() += next_increment());
  if (lhs_evaluations != 1 || rhs_evaluations != 1 ||
      !is_double_complex(result, 4.0, -2.0) ||
      !is_double_complex(selected_values[1], 4.0, -2.0))
    return 1;

  global_value = selected_values[1];
  result = (global_value -= CMPLX(1.0, -2.0));
  if (!is_double_complex(result, 3.0, 0.0) ||
      !is_double_complex(global_value, 3.0, 0.0))
    return 2;

  result = (global_value *= CMPLX(2.0, -1.0));
  if (!is_double_complex(result, 6.0, -3.0) ||
      !is_double_complex(global_value, 6.0, -3.0))
    return 3;

  result = (global_value /= 2.0);
  if (!is_double_complex(result, 3.0, -1.5) ||
      !is_double_complex(global_value, 3.0, -1.5))
    return 4;

  _Atomic(double complex) local_value = CMPLX(1.0, 1.0);
  _Atomic(double complex) *pointer = &local_value;
  result = (*pointer *= CMPLX(2.0, -1.0));
  if (!is_double_complex(result, 3.0, 1.0) ||
      !is_double_complex(*pointer, 3.0, 1.0))
    return 5;

  long double complex long_result =
      (global_long_value += CMPLXL(1.0L, -1.0L));
  if (!is_long_complex(long_result, 3.0L, 2.0L))
    return 6;
  long_result = (global_long_value *= 2.0L);
  if (!is_long_complex(long_result, 6.0L, 4.0L))
    return 7;
  long_result = (global_long_value /= 2.0L);
  if (!is_long_complex(long_result, 3.0L, 2.0L))
    return 8;
  long_result =
      (global_long_value -= CMPLXL(3.0L, 2.0L));
  if (!is_long_complex(long_result, 0.0L, 0.0L) ||
      !is_long_complex(global_long_value, 0.0L, 0.0L))
    return 9;
  return 0;
}
