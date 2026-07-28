/*
 * Preserve the C11 math/tgmath contracts for the gamma and adjacent-value
 * families, including all three widths and the non-generic long-double
 * direction parameter of nexttoward.
 */
#include <assert.h>
#include <math.h>
#include <tgmath.h>

#define IS_TYPE(expression, type) \
  _Generic((expression), type: 1, default: 0)

_Static_assert(IS_TYPE(nextafter(1.0f, 2.0f), float),
               "nextafter float dispatch");
_Static_assert(IS_TYPE(nextafter(1.0f, 2.0), double),
               "nextafter common double dispatch");
_Static_assert(IS_TYPE(nextafter(1.0, 2.0L), long double),
               "nextafter common long double dispatch");
_Static_assert(IS_TYPE(nexttoward(1.0f, 2.0L), float),
               "nexttoward follows its first argument");
_Static_assert(IS_TYPE(nexttoward(1.0, 2.0L), double),
               "nexttoward double dispatch");
_Static_assert(IS_TYPE(nexttoward(1.0L, 2.0L), long double),
               "nexttoward long double dispatch");
_Static_assert(IS_TYPE(tgamma(5.0f), float), "tgamma float dispatch");
_Static_assert(IS_TYPE(tgamma(5), double), "tgamma integer dispatch");
_Static_assert(IS_TYPE(tgamma(5.0L), long double),
               "tgamma long double dispatch");
_Static_assert(IS_TYPE(lgamma(5.0f), float), "lgamma float dispatch");
_Static_assert(IS_TYPE(lgamma(5), double), "lgamma integer dispatch");
_Static_assert(IS_TYPE(lgamma(5.0L), long double),
               "lgamma long double dispatch");

static double (*nextafter_signature)(double, double) = (nextafter);
static float (*nextafterf_signature)(float, float) = (nextafterf);
static long double (*nextafterl_signature)(
    long double, long double) = (nextafterl);
static double (*nexttoward_signature)(double, long double) = (nexttoward);
static float (*nexttowardf_signature)(float, long double) = (nexttowardf);
static long double (*nexttowardl_signature)(
    long double, long double) = (nexttowardl);
static double (*tgamma_signature)(double) = (tgamma);
static float (*tgammaf_signature)(float) = (tgammaf);
static long double (*tgammal_signature)(long double) = (tgammal);
static double (*lgamma_signature)(double) = (lgamma);
static float (*lgammaf_signature)(float) = (lgammaf);
static long double (*lgammal_signature)(long double) = (lgammal);

static int first_count;
static int second_count;

static float next_float(float value) {
  first_count++;
  return value;
}

static double next_double(double value) {
  second_count++;
  return value;
}

static int close_enough(double actual, double expected, double tolerance) {
  double difference = actual - expected;
  if (difference < 0.0)
    difference = -difference;
  return difference <= tolerance;
}

int main(void) {
  double upward = nextafter_signature(1.0, 2.0);
  double downward = nextafter_signature(1.0, 0.0);
  float float_upward = nextafterf_signature(1.0f, 2.0f);
  long double long_upward = nextafterl_signature(1.0L, 2.0L);
  float toward_unrepresentable =
      nexttowardf_signature(1.0f, 1.0L + 0.0000000001L);

  assert(upward > 1.0);
  assert(downward < 1.0);
  assert(float_upward > 1.0f);
  assert(long_upward > 1.0L);
  assert(toward_unrepresentable > 1.0f);
  assert(nexttoward_signature(0.0, -1.0L) < 0.0);
  assert(nexttowardl_signature(0.0L, 1.0L) > 0.0L);
  assert(isnan(nexttowardf_signature(1.0f, (long double)NAN)));

  first_count = 0;
  second_count = 0;
  assert(nextafter(next_float(1.0f), next_double(2.0)) > 1.0);
  assert(first_count == 1);
  assert(second_count == 1);

  assert(close_enough(tgamma_signature(1.0), 1.0, 0.000000000001));
  assert(close_enough(tgammaf_signature(5.0f), 24.0, 0.0001));
  assert(close_enough((double)tgammal_signature(6.0L), 120.0,
                      0.000000001));
  assert(close_enough(lgamma_signature(1.0), 0.0, 0.000000000001));
  assert(close_enough(lgammaf_signature(5.0f), 3.17805383, 0.0001));
  assert(close_enough((double)lgammal_signature(6.0L), 4.78749174278,
                      0.000000001));
  assert(close_enough(tgamma(4), 6.0, 0.000000001));
  assert(close_enough(lgamma(4.0), 1.79175946923, 0.000000001));
  assert(isnan(tgamma_signature(-1.0)));
  assert(isinf(lgamma_signature(0.0)));
  assert(isinf(tgamma_signature(INFINITY)));
  assert(isinf(tgamma_signature(-0.0)));
  assert(signbit(tgamma_signature(-0.0)));
  return 0;
}
