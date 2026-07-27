// C11 <tgmath.h> dispatches the complex-capable function families by both
// width and real/complex domain. Integer generic arguments contribute double
// width, and _Generic control expressions must not evaluate their operands.
#include <complex.h>
#include <tgmath.h>

#define IS_TYPE(expression, type) \
  _Generic((expression), type: 1, default: 0)
#define ASSERT_FLOAT_COMPLEX_UNARY(function) \
  _Static_assert( \
      IS_TYPE(function(float_value), float _Complex), \
      #function "(float complex) returns float complex")

static int complex_call_count;
static int real_call_count;

static float _Complex next_complex(void) {
  complex_call_count++;
  return CMPLXF(0.0f, 1.0f);
}

static double next_real(void) {
  real_call_count++;
  return 2.0;
}

static int close_enough(double actual, double expected) {
  double difference = actual - expected;
  if (difference < 0.0)
    difference = -difference;
  return difference < 0.0001;
}

int main(void) {
  float _Complex float_value = CMPLXF(3.0f, 4.0f);
  double _Complex double_value = CMPLX(0.0, 1.0);
  double _Complex negative_four = -4.0 + 0.0 * I;
  long double _Complex long_double_value =
      CMPLXL(0.5L, 0.0L);

  ASSERT_FLOAT_COMPLEX_UNARY(acos);
  ASSERT_FLOAT_COMPLEX_UNARY(asin);
  ASSERT_FLOAT_COMPLEX_UNARY(atan);
  ASSERT_FLOAT_COMPLEX_UNARY(acosh);
  ASSERT_FLOAT_COMPLEX_UNARY(asinh);
  ASSERT_FLOAT_COMPLEX_UNARY(atanh);
  ASSERT_FLOAT_COMPLEX_UNARY(cos);
  ASSERT_FLOAT_COMPLEX_UNARY(sin);
  ASSERT_FLOAT_COMPLEX_UNARY(tan);
  ASSERT_FLOAT_COMPLEX_UNARY(cosh);
  ASSERT_FLOAT_COMPLEX_UNARY(sinh);
  ASSERT_FLOAT_COMPLEX_UNARY(tanh);
  ASSERT_FLOAT_COMPLEX_UNARY(exp);
  ASSERT_FLOAT_COMPLEX_UNARY(log);
  ASSERT_FLOAT_COMPLEX_UNARY(sqrt);

  _Static_assert(
      IS_TYPE(exp(double_value), double _Complex),
      "exp(double complex) returns double complex");
  _Static_assert(
      IS_TYPE(atan(long_double_value), long double _Complex),
      "atan(long double complex) returns long double complex");
  _Static_assert(
      IS_TYPE(pow(float_value, 2.0f), float _Complex),
      "float complex and float select float complex");
  _Static_assert(
      IS_TYPE(pow(float_value, 2), double _Complex),
      "integer generic argument contributes double width");
  _Static_assert(
      IS_TYPE(pow(float_value, 2.0L), long double _Complex),
      "long double generic argument contributes long double width");
  _Static_assert(
      IS_TYPE(fabs(float_value), float),
      "fabs(float complex) returns float");

  _Static_assert(
      IS_TYPE(carg(1.0f), float),
      "carg(float) returns float");
  _Static_assert(
      IS_TYPE(cimag(1), double),
      "cimag(integer) returns double");
  _Static_assert(
      IS_TYPE(conj(1.0f), float _Complex),
      "conj(float) returns float complex");
  _Static_assert(
      IS_TYPE(cproj(1.0L), long double _Complex),
      "cproj(long double) returns long double complex");
  _Static_assert(
      IS_TYPE(creal(float_value), float),
      "creal(float complex) returns float");

  if (!close_enough(fabs(float_value), 5.0))
    return 1;

  double _Complex square = pow(double_value, 2.0);
  if (!close_enough(creal(square), -1.0) ||
      !close_enough(cimag(square), 0.0))
    return 2;

  double _Complex root = sqrt(negative_four);
  if (!close_enough(creal(root), 0.0) ||
      !close_enough(cimag(root), 2.0))
    return 3;

  complex_call_count = 0;
  float _Complex selected_root = sqrt(next_complex());
  if (complex_call_count != 1 ||
      !close_enough(crealf(selected_root), 0.70710678) ||
      !close_enough(cimagf(selected_root), 0.70710678))
    return 4;

  complex_call_count = 0;
  real_call_count = 0;
  double _Complex mixed_power =
      pow(next_complex(), next_real());
  if (complex_call_count != 1 || real_call_count != 1 ||
      !close_enough(creal(mixed_power), -1.0) ||
      !close_enough(cimag(mixed_power), 0.0))
    return 5;

  real_call_count = 0;
  float _Complex conjugated = conj((float)next_real());
  if (real_call_count != 1 ||
      !close_enough(crealf(conjugated), 2.0) ||
      !close_enough(cimagf(conjugated), 0.0))
    return 6;

  return 0;
}
