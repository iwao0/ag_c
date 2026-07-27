// C11 <complex.h> inverse trigonometric and hyperbolic functions must expose
// all float/double/long-double entry points with complex value ABI.
#include <complex.h>

typedef float _Complex (*float_inverse_function)(float _Complex);
typedef double _Complex (*double_inverse_function)(double _Complex);
typedef long double _Complex (*long_double_inverse_function)(
    long double _Complex);

static float_inverse_function float_functions[] = {
    cacosf, casinf, catanf, cacoshf, casinhf, catanhf};
static double_inverse_function double_functions[] = {
    cacos, casin, catan, cacosh, casinh, catanh};
static long_double_inverse_function long_double_functions[] = {
    cacosl, casinl, catanl, cacoshl, casinhl, catanhl};

static int close_enough(long double actual, long double expected) {
  long double difference = actual - expected;
  if (difference < 0.0L)
    difference = -difference;
  return difference < 0.0001L;
}

static int close_complex(
    long double _Complex actual,
    long double expected_real,
    long double expected_imaginary) {
  return close_enough(creall(actual), expected_real) &&
         close_enough(cimagl(actual), expected_imaginary);
}

int main(void) {
  double _Complex double_inputs[] = {
      CMPLX(0.0, 0.0),
      CMPLX(0.0, 1.0),
      CMPLX(0.5, 0.0),
      CMPLX(2.0, 0.0),
      CMPLX(0.0, 0.5),
      CMPLX(0.5, 0.0)};
  long double expected_real[] = {
      1.570796326794897L,
      0.0L,
      0.463647609000806L,
      1.316957896924817L,
      0.0L,
      0.549306144334055L};
  long double expected_imaginary[] = {
      0.0L,
      0.881373587019543L,
      0.0L,
      0.0L,
      0.523598775598299L,
      0.0L};

  for (int index = 0; index < 6; index++) {
    if (!close_complex(
            double_functions[index](double_inputs[index]),
            expected_real[index], expected_imaginary[index]))
      return 1 + index;
  }

  float _Complex float_zero = CMPLXF(0.0f, 0.0f);
  float _Complex float_one = CMPLXF(1.0f, 0.0f);
  for (int index = 0; index < 6; index++) {
    float _Complex argument =
        index == 3 ? float_one : float_zero;
    float _Complex result = float_functions[index](argument);
    float expected = index == 0 ? 1.5707963f : 0.0f;
    if (!close_enough(crealf(result), expected) ||
        !close_enough(cimagf(result), 0.0f))
      return 10 + index;
  }

  long double _Complex long_double_zero =
      CMPLXL(0.0L, 0.0L);
  long double _Complex long_double_one =
      CMPLXL(1.0L, 0.0L);
  for (int index = 0; index < 6; index++) {
    long double _Complex argument =
        index == 3 ? long_double_one : long_double_zero;
    long double _Complex result =
        long_double_functions[index](argument);
    long double expected =
        index == 0 ? 1.570796326794897L : 0.0L;
    if (!close_complex(result, expected, 0.0L))
      return 20 + index;
  }
  return 0;
}
