// 16-byte atomic double/long-double complex load/store boundaries.
// Expected: exit=0
#include <complex.h>

static _Atomic(double complex) global_value =
    CMPLX(1.25, -2.5);
static _Atomic(long double complex) global_long_value =
    CMPLXL(3.5L, -4.75L);
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

static double complex next_value(void) {
  rhs_evaluations++;
  return CMPLX(9.25, -10.5);
}

static double complex snapshot_value(
    _Atomic(double complex) *pointer) {
  return *pointer;
}

int main(void) {
  double complex snapshot = global_value;
  long double complex long_snapshot = global_long_value;
  if (!is_double_complex(snapshot, 1.25, -2.5) ||
      !is_long_complex(long_snapshot, 3.5L, -4.75L))
    return 1;

  double complex assignment_result =
      (global_value = CMPLX(5.25, -6.5));
  snapshot = global_value;
  if (!is_double_complex(assignment_result, 5.25, -6.5) ||
      !is_double_complex(snapshot, 5.25, -6.5))
    return 2;

  long double complex long_assignment =
      (global_long_value = CMPLXL(7.5L, -8.75L));
  long_snapshot = global_long_value;
  if (!is_long_complex(long_assignment, 7.5L, -8.75L) ||
      !is_long_complex(long_snapshot, 7.5L, -8.75L))
    return 3;

  _Atomic(double complex) local_value =
      CMPLX(11.25, -12.5);
  snapshot = local_value;
  if (!is_double_complex(snapshot, 11.25, -12.5))
    return 4;

  _Atomic(double complex) *pointer = &local_value;
  assignment_result = (*pointer = CMPLX(13.25, -14.5));
  snapshot = *pointer;
  if (!is_double_complex(assignment_result, 13.25, -14.5) ||
      !is_double_complex(snapshot, 13.25, -14.5))
    return 5;

  selected_values[0] = CMPLX(15.25, -16.5);
  snapshot = selected_values[0];
  if (!is_double_complex(snapshot, 15.25, -16.5))
    return 6;

  assignment_result = (*selected_value() = next_value());
  if (lhs_evaluations != 1 || rhs_evaluations != 1 ||
      !is_double_complex(assignment_result, 9.25, -10.5) ||
      !is_double_complex(selected_values[1], 9.25, -10.5) ||
      !is_double_complex(
          snapshot_value(&selected_values[1]), 9.25, -10.5))
    return 7;

  global_value = selected_values[1];
  snapshot = global_value;
  if (!is_double_complex(snapshot, 9.25, -10.5))
    return 8;
  return 0;
}
