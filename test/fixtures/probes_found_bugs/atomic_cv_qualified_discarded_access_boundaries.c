// Discarded const/volatile atomic object access boundaries.
// Expected: exit=0
#include <complex.h>

struct small {
  int value;
};

struct wide {
  long long left;
  long long right;
};

static int values[3] = {3, 5, 7};

static const _Atomic int constant_integer = 11;
static volatile _Atomic double volatile_real = -13.0;
static const _Atomic(int *) constant_pointer = values + 1;
static volatile _Atomic(int *) volatile_pointer = values + 2;
static const _Atomic(struct small) constant_small =
    (struct small){17};
static volatile _Atomic(struct wide) volatile_wide =
    (struct wide){19, 23};
static const _Atomic(float complex) constant_float_complex =
    CMPLXF(29.0f, -31.0f);
static volatile _Atomic(double complex) volatile_double_complex =
    CMPLX(37.0, -41.0);

static int integer_selector_evaluations;
static int pointer_selector_evaluations;
static int wide_selector_evaluations;
static int complex_selector_evaluations;

static const _Atomic int *select_constant_integer(void) {
  integer_selector_evaluations++;
  return &constant_integer;
}

static volatile _Atomic(int *) *select_volatile_pointer(void) {
  pointer_selector_evaluations++;
  return &volatile_pointer;
}

static volatile _Atomic(struct wide) *select_volatile_wide(void) {
  wide_selector_evaluations++;
  return &volatile_wide;
}

static const _Atomic(float complex) *
select_constant_float_complex(void) {
  complex_selector_evaluations++;
  return &constant_float_complex;
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
  const _Atomic unsigned short local_constant_short = 43;
  volatile _Atomic float local_volatile_float = -47.0f;
  const _Atomic(struct small) local_constant_small =
      (struct small){53};
  volatile _Atomic(struct wide) local_volatile_wide =
      (struct wide){59, 61};
  const _Atomic(float complex) local_constant_complex =
      CMPLXF(67.0f, -71.0f);
  volatile _Atomic(double complex) local_volatile_complex =
      CMPLX(73.0, -79.0);

  (void)constant_integer;
  (void)volatile_real;
  (void)constant_pointer;
  (void)volatile_pointer;
  (void)constant_small;
  (void)volatile_wide;
  (void)constant_float_complex;
  (void)volatile_double_complex;

  (void)local_constant_short;
  (void)local_volatile_float;
  (void)local_constant_small;
  (void)local_volatile_wide;
  (void)local_constant_complex;
  (void)local_volatile_complex;

  (void)*select_constant_integer();
  (void)*select_volatile_pointer();
  (void)*select_volatile_wide();
  (void)*select_constant_float_complex();

  if (integer_selector_evaluations != 1 ||
      pointer_selector_evaluations != 1 ||
      wide_selector_evaluations != 1 ||
      complex_selector_evaluations != 1)
    return 1;

  int integer_snapshot = constant_integer;
  double real_snapshot = volatile_real;
  int *constant_pointer_snapshot = constant_pointer;
  int *volatile_pointer_snapshot = volatile_pointer;
  struct small small_snapshot = constant_small;
  struct wide wide_snapshot = volatile_wide;
  float complex float_complex_snapshot =
      constant_float_complex;
  double complex double_complex_snapshot =
      volatile_double_complex;

  unsigned short local_short_snapshot =
      local_constant_short;
  float local_float_snapshot = local_volatile_float;
  struct small local_small_snapshot = local_constant_small;
  struct wide local_wide_snapshot = local_volatile_wide;
  float complex local_float_complex_snapshot =
      local_constant_complex;
  double complex local_double_complex_snapshot =
      local_volatile_complex;

  if (integer_snapshot != 11 || real_snapshot != -13.0 ||
      constant_pointer_snapshot != values + 1 ||
      volatile_pointer_snapshot != values + 2 ||
      small_snapshot.value != 17 ||
      wide_snapshot.left != 19 || wide_snapshot.right != 23 ||
      !is_float_complex(
          float_complex_snapshot, 29.0f, -31.0f) ||
      !is_double_complex(
          double_complex_snapshot, 37.0, -41.0))
    return 2;

  if (local_short_snapshot != 43 ||
      local_float_snapshot != -47.0f ||
      local_small_snapshot.value != 53 ||
      local_wide_snapshot.left != 59 ||
      local_wide_snapshot.right != 61 ||
      !is_float_complex(
          local_float_complex_snapshot, 67.0f, -71.0f) ||
      !is_double_complex(
          local_double_complex_snapshot, 73.0, -79.0))
    return 3;
  return 0;
}
