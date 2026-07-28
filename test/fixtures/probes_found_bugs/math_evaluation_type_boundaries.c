/*
 * C11 <math.h> exposes float_t and double_t according to FLT_EVAL_METHOD.
 * ag_c evaluates expressions in their semantic float/double widths, so method
 * zero requires these typedefs to preserve those exact identities.
 */
#include <assert.h>
#include <math.h>
#include <math.h>
#include <float.h>

_Static_assert(FLT_EVAL_METHOD == 0, "target evaluation method");
_Static_assert(_Generic((float_t)0, float: 1, default: 0),
               "float_t identity for evaluation method zero");
_Static_assert(_Generic((double_t)0, double: 1, default: 0),
               "double_t identity for evaluation method zero");
_Static_assert(sizeof(float_t) == sizeof(float), "float_t width");
_Static_assert(_Alignof(float_t) == _Alignof(float), "float_t alignment");
_Static_assert(sizeof(double_t) == sizeof(double), "double_t width");
_Static_assert(_Alignof(double_t) == _Alignof(double), "double_t alignment");

static float_t (*sqrtf_evaluation_signature)(float_t) = sqrtf;
static double_t (*sqrt_evaluation_signature)(double_t) = sqrt;
static float_t (*modff_evaluation_signature)(float_t, float_t *) = modff;
static double_t (*modf_evaluation_signature)(double_t, double_t *) = modf;

int main(void) {
  float_t float_integer = 0.0f;
  double_t double_integer = 0.0;
  float_t float_fraction =
      modff_evaluation_signature(3.75f, &float_integer);
  double_t double_fraction =
      modf_evaluation_signature(-6.25, &double_integer);

  assert(sqrtf_evaluation_signature(9.0f) == 3.0f);
  assert(sqrt_evaluation_signature(16.0) == 4.0);
  assert(float_integer == 3.0f);
  assert(float_fraction == 0.75f);
  assert(double_integer == -6.0);
  assert(double_fraction == -0.25);
  assert(fpclassify((long double)1.0L) == FP_NORMAL);
  assert(isfinite(1.0f));
  assert(isfinite((long double)1.0L));
  assert(!signbit(1.0f));
  assert(signbit((long double)-0.0L));
  assert(isgreater(2.0, 1.0));
  assert(isgreaterequal(2.0, 2.0));
  assert(isless(1.0, 2.0));
  assert(islessequal(2.0, 2.0));
  assert(islessgreater(1.0, 2.0));
  assert(!isunordered(1.0, 2.0));
  return 0;
}
