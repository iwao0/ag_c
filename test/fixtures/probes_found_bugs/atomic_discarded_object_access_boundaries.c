// Discarded atomic aggregate and complex lvalue-conversion boundaries.
// Expected: exit=0
#include <complex.h>

struct small {
  int value;
};

struct wide {
  long long left;
  long long right;
};

struct holder {
  _Atomic(struct small) small;
  _Atomic(float complex) complex_value;
};

static _Atomic(struct small) global_small =
    (struct small){11};
static _Atomic(struct wide) global_wide =
    (struct wide){12, 13};
static _Atomic(float complex) global_float_complex =
    CMPLXF(14.0f, -15.0f);
static _Atomic(double complex) global_double_complex =
    CMPLX(16.0, -17.0);
static _Atomic(struct small) small_values[2] = {
    (struct small){18}, (struct small){19}};
static _Atomic(double complex) complex_values[2] = {
    CMPLX(20.0, -21.0), CMPLX(22.0, -23.0)};
static struct holder global_holder = {
    (struct small){24}, CMPLXF(25.0f, -26.0f)};

static int small_selector_evaluations;
static int wide_selector_evaluations;
static int float_selector_evaluations;
static int double_selector_evaluations;

static _Atomic(struct small) *select_small(void) {
  small_selector_evaluations++;
  return &small_values[1];
}

static _Atomic(struct wide) *select_wide(void) {
  wide_selector_evaluations++;
  return &global_wide;
}

static _Atomic(float complex) *select_float_complex(void) {
  float_selector_evaluations++;
  return &global_float_complex;
}

static _Atomic(double complex) *select_double_complex(void) {
  double_selector_evaluations++;
  return &complex_values[1];
}

static int is_float_complex(
    float complex value, float real, float imag) {
  return crealf(value) == real && cimagf(value) == imag;
}

static int is_double_complex(
    double complex value, double real, double imag) {
  return creal(value) == real && cimag(value) == imag;
}

int main(void) {
  _Atomic(struct small) local_small =
      (struct small){27};
  _Atomic(struct wide) local_wide =
      (struct wide){28, 29};
  _Atomic(float complex) local_float_complex =
      CMPLXF(30.0f, -31.0f);
  _Atomic(double complex) local_double_complex =
      CMPLX(32.0, -33.0);

  _Atomic(struct small) *small_pointer = &local_small;
  _Atomic(struct wide) *wide_pointer = &local_wide;
  _Atomic(float complex) *float_pointer =
      &local_float_complex;
  _Atomic(double complex) *double_pointer =
      &local_double_complex;
  _Atomic(struct small) * volatile volatile_pointer =
      &global_small;

  (void)global_small;
  (void)global_wide;
  (void)global_float_complex;
  (void)global_double_complex;

  (void)local_small;
  (void)local_wide;
  (void)local_float_complex;
  (void)local_double_complex;

  (void)*small_pointer;
  (void)*wide_pointer;
  (void)*float_pointer;
  (void)*double_pointer;
  (void)*volatile_pointer;

  (void)small_values[0];
  (void)complex_values[0];
  (void)global_holder.small;
  (void)global_holder.complex_value;

  (void)*select_small();
  (void)*select_wide();
  (void)*select_float_complex();
  (void)*select_double_complex();

  if (small_selector_evaluations != 1 ||
      wide_selector_evaluations != 1 ||
      float_selector_evaluations != 1 ||
      double_selector_evaluations != 1)
    return 1;

  struct small small_snapshot = global_small;
  struct wide wide_snapshot = global_wide;
  float complex float_snapshot = global_float_complex;
  double complex double_snapshot = global_double_complex;
  struct small local_small_snapshot = local_small;
  struct wide local_wide_snapshot = local_wide;
  float complex local_float_snapshot = local_float_complex;
  double complex local_double_snapshot = local_double_complex;
  struct small selected_small_snapshot = small_values[1];
  double complex selected_double_snapshot = complex_values[1];
  struct small member_snapshot = global_holder.small;
  float complex member_complex_snapshot =
      global_holder.complex_value;

  if (small_snapshot.value != 11 ||
      wide_snapshot.left != 12 || wide_snapshot.right != 13 ||
      !is_float_complex(float_snapshot, 14.0f, -15.0f) ||
      !is_double_complex(double_snapshot, 16.0, -17.0))
    return 2;
  if (local_small_snapshot.value != 27 ||
      local_wide_snapshot.left != 28 ||
      local_wide_snapshot.right != 29 ||
      !is_float_complex(local_float_snapshot, 30.0f, -31.0f) ||
      !is_double_complex(local_double_snapshot, 32.0, -33.0))
    return 3;
  if (selected_small_snapshot.value != 19 ||
      !is_double_complex(
          selected_double_snapshot, 22.0, -23.0) ||
      member_snapshot.value != 24 ||
      !is_float_complex(
          member_complex_snapshot, 25.0f, -26.0f))
    return 4;
  return 0;
}
