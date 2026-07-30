// ag_c atomic compound-literal extension boundaries.
// Expected: exit=0
#include <complex.h>

struct small {
  int value;
};

struct wide {
  long long left;
  long long right;
};

union word {
  unsigned int bits;
  float value;
};

static int values[3] = {3, 5, 7};
static int initializer_evaluations;

static int next_integer(void) {
  initializer_evaluations++;
  return 11;
}

static int *next_pointer(void) {
  initializer_evaluations++;
  return values + 1;
}

static float next_float(void) {
  initializer_evaluations++;
  return 13.0f;
}

static int next_small_member(void) {
  initializer_evaluations++;
  return 17;
}

static long long next_wide_member(long long value) {
  initializer_evaluations++;
  return value;
}

static unsigned int next_union_bits(void) {
  initializer_evaluations++;
  return 0x12345678u;
}

static float complex next_float_complex(void) {
  initializer_evaluations++;
  return CMPLXF(29.0f, -31.0f);
}

static double complex next_double_complex(void) {
  initializer_evaluations++;
  return CMPLX(37.0, -41.0);
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
  (void)(_Atomic int){next_integer()};
  (void)(_Atomic(int *)){next_pointer()};
  (void)(_Atomic float){next_float()};
  (void)(_Atomic(struct small)){next_small_member()};
  (void)(_Atomic(struct wide)){
      next_wide_member(19), next_wide_member(23)};
  (void)(_Atomic(union word)){
      .bits = next_union_bits()};
  (void)(_Atomic(float complex)){next_float_complex()};
  (void)(_Atomic(double complex)){next_double_complex()};

  if (initializer_evaluations != 9)
    return 1;

  _Atomic int *integer_object =
      &(_Atomic int){43};
  _Atomic(int *) *pointer_object =
      &(_Atomic(int *)){values + 2};
  _Atomic(struct small) *small_object =
      &(_Atomic(struct small)){47};
  _Atomic(struct wide) *wide_object =
      &(_Atomic(struct wide)){53, 59};
  _Atomic(union word) *union_object =
      &(_Atomic(union word)){.bits = 0x89abcdefu};
  _Atomic(float complex) *float_complex_object =
      &(_Atomic(float complex)){CMPLXF(61.0f, -67.0f)};
  _Atomic(double complex) *double_complex_object =
      &(_Atomic(double complex)){CMPLX(71.0, -73.0)};

  if (*integer_object != 43 ||
      *pointer_object != values + 2 ||
      (*small_object).value != 47)
    return 2;

  struct wide wide_snapshot = *wide_object;
  union word union_snapshot = *union_object;
  float complex float_snapshot = *float_complex_object;
  double complex double_snapshot = *double_complex_object;
  if (wide_snapshot.left != 53 || wide_snapshot.right != 59 ||
      union_snapshot.bits != 0x89abcdefu ||
      !is_float_complex(float_snapshot, 61.0f, -67.0f) ||
      !is_double_complex(double_snapshot, 71.0, -73.0))
    return 3;

  int integer_result = (*integer_object = 79);
  int *pointer_result = (*pointer_object = values);
  struct small small_result =
      (*small_object = (struct small){83});
  struct wide wide_result =
      (*wide_object = (struct wide){89, 97});
  union word union_result =
      (*union_object = (union word){.bits = 0xfedcba98u});
  float complex float_result =
      (*float_complex_object = CMPLXF(101.0f, -103.0f));
  double complex double_result =
      (*double_complex_object = CMPLX(107.0, -109.0));

  if (integer_result != 79 || *integer_object != 79 ||
      pointer_result != values || *pointer_object != values ||
      small_result.value != 83 ||
      (*small_object).value != 83)
    return 4;

  wide_snapshot = *wide_object;
  union_snapshot = *union_object;
  float_snapshot = *float_complex_object;
  double_snapshot = *double_complex_object;
  if (wide_result.left != 89 || wide_result.right != 97 ||
      wide_snapshot.left != 89 || wide_snapshot.right != 97 ||
      union_result.bits != 0xfedcba98u ||
      union_snapshot.bits != 0xfedcba98u ||
      !is_float_complex(float_result, 101.0f, -103.0f) ||
      !is_float_complex(float_snapshot, 101.0f, -103.0f) ||
      !is_double_complex(double_result, 107.0, -109.0) ||
      !is_double_complex(double_snapshot, 107.0, -109.0))
    return 5;
  return 0;
}
