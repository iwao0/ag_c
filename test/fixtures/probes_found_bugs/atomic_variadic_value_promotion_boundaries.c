// Atomic lvalue conversion and default argument promotion across varargs.
// Expected: exit=0
#include <complex.h>
#include <stdarg.h>

struct small {
  int value;
};

struct wide {
  long long left;
  long long right;
};

union choice {
  unsigned long long bits;
  double value;
};

static int values[3] = {3, 5, 7};

static _Atomic _Bool atomic_boolean = 1;
static _Atomic signed char atomic_byte = -11;
static _Atomic unsigned short atomic_short = 65000;
static _Atomic float atomic_float = 13.25f;
static _Atomic double atomic_double = -17.5;
static _Atomic(int *) atomic_pointer = values + 1;
static _Atomic(float complex) atomic_float_complex =
    CMPLXF(19.0f, -23.0f);
static _Atomic(double complex) atomic_double_complex =
    CMPLX(29.0, -31.0);
static _Atomic(struct small) atomic_small =
    (struct small){37};
static _Atomic(struct wide) atomic_wide =
    (struct wide){41, 43};
static _Atomic(union choice) atomic_choice =
    (union choice){.bits = 0x1122334455667788ULL};

static int scalar_selector_evaluations;
static int complex_selector_evaluations;
static int aggregate_selector_evaluations;

static _Atomic float *select_float(void) {
  scalar_selector_evaluations++;
  return &atomic_float;
}

static _Atomic(double complex) *select_double_complex(void) {
  complex_selector_evaluations++;
  return &atomic_double_complex;
}

static _Atomic(struct wide) *select_wide(void) {
  aggregate_selector_evaluations++;
  return &atomic_wide;
}

static int check_atomic_varargs(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);

  int boolean_value = va_arg(arguments, int);
  int byte_value = va_arg(arguments, int);
  int short_value = va_arg(arguments, int);
  double float_value = va_arg(arguments, double);
  double double_value = va_arg(arguments, double);
  int *pointer_value = va_arg(arguments, int *);
  float complex float_complex_value =
      va_arg(arguments, float complex);
  double complex double_complex_value =
      va_arg(arguments, double complex);
  struct small small_value =
      va_arg(arguments, struct small);
  struct wide wide_value =
      va_arg(arguments, struct wide);
  union choice choice_value =
      va_arg(arguments, union choice);

  va_end(arguments);

  if (marker != 101)
    return 1;
  if (boolean_value != 1 || byte_value != -11 ||
      short_value != 65000)
    return 2;
  if (float_value != 13.25)
    return 3;
  if (double_value != -17.5)
    return 4;
  if (pointer_value != values + 1)
    return 5;
  if (crealf(float_complex_value) != 19.0f ||
      cimagf(float_complex_value) != -23.0f)
    return 6;
  if (creal(double_complex_value) != 29.0 ||
      cimag(double_complex_value) != -31.0)
    return 7;
  if (small_value.value != 37 ||
      wide_value.left != 41 || wide_value.right != 43)
    return 8;
  if (choice_value.bits != 0x1122334455667788ULL)
    return 9;
  return 0;
}

int main(void) {
  int status = check_atomic_varargs(
      101, atomic_boolean, atomic_byte, atomic_short,
      *select_float(), atomic_double, atomic_pointer,
      atomic_float_complex, *select_double_complex(),
      atomic_small, *select_wide(), atomic_choice);
  if (status != 0)
    return status;
  if (scalar_selector_evaluations != 1 ||
      complex_selector_evaluations != 1 ||
      aggregate_selector_evaluations != 1)
    return 10;
  return 0;
}
