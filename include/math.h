#ifndef _MATH_H
#define _MATH_H

#ifndef __AGC_HUGE_VAL__
#define __AGC_HUGE_VAL__ __builtin_huge_val()
#define __AGC_HUGE_VALF__ __builtin_huge_valf()
#define __AGC_HUGE_VALL__ __builtin_huge_vall()
#define __AGC_NANF__ __builtin_nanf("")
#endif

#define HUGE_VAL  __AGC_HUGE_VAL__
#define HUGE_VALF __AGC_HUGE_VALF__
#define HUGE_VALL __AGC_HUGE_VALL__
#define INFINITY  HUGE_VALF
#define NAN       __AGC_NANF__

#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define FP_ILOGB0 (-2147483647 - 1)
#define FP_ILOGBNAN (-2147483647 - 1)

#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2
#ifdef __wasm32__
#define math_errhandling MATH_ERREXCEPT
#else
int __math_errhandling(void);
#define math_errhandling (__math_errhandling())
#endif

double acos(double x);
double asin(double x);
double atan(double x);
double atan2(double y, double x);
double cos(double x);
double sin(double x);
double tan(double x);

double cosh(double x);
double sinh(double x);
double tanh(double x);
double acosh(double x);
double asinh(double x);
double atanh(double x);
float coshf(float x);
long double coshl(long double x);
float sinhf(float x);
long double sinhl(long double x);
float tanhf(float x);
long double tanhl(long double x);
float acoshf(float x);
long double acoshl(long double x);
float asinhf(float x);
long double asinhl(long double x);
float atanhf(float x);
long double atanhl(long double x);

double exp(double x);
double exp2(double x);
double expm1(double x);
double erf(double x);
double erfc(double x);
double log(double x);
double log1p(double x);
double log10(double x);
double log2(double x);

double pow(double x, double y);
double sqrt(double x);
double cbrt(double x);
float powf(float x, float y);
long double powl(long double x, long double y);
float cbrtf(float x);
long double cbrtl(long double x);
long double sqrtl(long double x);

double ceil(double x);
double floor(double x);
double round(double x);
double trunc(double x);
double nearbyint(double x);
double rint(double x);
long lrint(double x);
long long llrint(double x);
long lround(double x);
long long llround(double x);

double fabs(double x);
double fmod(double x, double y);
double remainder(double x, double y);
double remquo(double x, double y, int *quo);
double fdim(double x, double y);
double fma(double x, double y, double z);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double scalbn(double x, int exp);
double scalbln(double x, long exp);
int ilogb(double x);
double logb(double x);
double modf(double x, double *iptr);
double copysign(double x, double y);
double nan(const char *tagp);
double hypot(double x, double y);
double fmin(double x, double y);
double fmax(double x, double y);

float sinf(float x);
long double sinl(long double x);
float cosf(float x);
long double cosl(long double x);
float tanf(float x);
long double tanl(long double x);
float asinf(float x);
long double asinl(long double x);
float acosf(float x);
long double acosl(long double x);
float atanf(float x);
long double atanl(long double x);
float atan2f(float y, float x);
long double atan2l(long double y, long double x);
float expf(float x);
long double expl(long double x);
float exp2f(float x);
long double exp2l(long double x);
float expm1f(float x);
long double expm1l(long double x);
float erff(float x);
long double erfl(long double x);
float erfcf(float x);
long double erfcl(long double x);
float logf(float x);
long double logl(long double x);
float log1pf(float x);
long double log1pl(long double x);
float log10f(float x);
long double log10l(long double x);
float log2f(float x);
long double log2l(long double x);
float fabsf(float x);
long double fabsl(long double x);
float sqrtf(float x);
float ceilf(float x);
long double ceill(long double x);
float floorf(float x);
long double floorl(long double x);
float roundf(float x);
long double roundl(long double x);
float truncf(float x);
long double truncl(long double x);
float nearbyintf(float x);
long double nearbyintl(long double x);
float rintf(float x);
long double rintl(long double x);
long lrintf(float x);
long lrintl(long double x);
long long llrintf(float x);
long long llrintl(long double x);
long lroundf(float x);
long lroundl(long double x);
long long llroundf(float x);
long long llroundl(long double x);
float fmodf(float x, float y);
long double fmodl(long double x, long double y);
float remainderf(float x, float y);
long double remainderl(long double x, long double y);
float remquof(float x, float y, int *quo);
long double remquol(long double x, long double y, int *quo);
float fdimf(float x, float y);
long double fdiml(long double x, long double y);
float fmaf(float x, float y, float z);
long double fmal(long double x, long double y, long double z);
float frexpf(float x, int *exp);
long double frexpl(long double x, int *exp);
float ldexpf(float x, int exp);
long double ldexpl(long double x, int exp);
float scalbnf(float x, int exp);
long double scalbnl(long double x, int exp);
float scalblnf(float x, long exp);
long double scalblnl(long double x, long exp);
int ilogbf(float x);
int ilogbl(long double x);
float logbf(float x);
long double logbl(long double x);
float modff(float x, float *iptr);
long double modfl(long double x, long double *iptr);
float copysignf(float x, float y);
long double copysignl(long double x, long double y);
float nanf(const char *tagp);
long double nanl(const char *tagp);
float hypotf(float x, float y);
long double hypotl(long double x, long double y);
float fminf(float x, float y);
long double fminl(long double x, long double y);
float fmaxf(float x, float y);
long double fmaxl(long double x, long double y);

int fpclassify(double x);
int isfinite(double x);
int isinf(double x);
int isnan(double x);
int isnormal(double x);
int signbit(double x);
int isgreater(double x, double y);
int isgreaterequal(double x, double y);
int isless(double x, double y);
int islessequal(double x, double y);
int islessgreater(double x, double y);
int isunordered(double x, double y);

/*
 * C11 7.12.3 / 7.12.14 defines these interfaces as macros.  Darwin does not
 * provide linkable functions for every macro name, so calls emitted from this
 * bundled header must not depend on external fpclassify/isfinite/etc symbols.
 * Keep float subnormals distinct before promotion; ag_c's long double shares
 * the target double representation.
 */
union __ag_math_float_bits {
  float value;
  unsigned int bits;
};

union __ag_math_double_bits {
  double value;
  unsigned long long bits;
};

static int __ag_math_fpclassify_float(float value) {
  union __ag_math_float_bits repr;
  unsigned int exponent;
  unsigned int fraction;
  repr.value = value;
  exponent = (repr.bits >> 23) & 0xffU;
  fraction = repr.bits & 0x7fffffU;
  if (exponent == 0xffU) return fraction ? FP_NAN : FP_INFINITE;
  if (exponent == 0U) return fraction ? FP_SUBNORMAL : FP_ZERO;
  return FP_NORMAL;
}

static int __ag_math_fpclassify_double(double value) {
  union __ag_math_double_bits repr;
  unsigned long long exponent;
  unsigned long long fraction;
  repr.value = value;
  exponent = (repr.bits >> 52) & 0x7ffULL;
  fraction = repr.bits & 0xfffffffffffffULL;
  if (exponent == 0x7ffULL) return fraction ? FP_NAN : FP_INFINITE;
  if (exponent == 0ULL) return fraction ? FP_SUBNORMAL : FP_ZERO;
  return FP_NORMAL;
}

static int __ag_math_fpclassify_long_double(long double value) {
  return __ag_math_fpclassify_double((double)value);
}

static int __ag_math_isfinite_float(float value) {
  int classification = __ag_math_fpclassify_float(value);
  return classification != FP_NAN && classification != FP_INFINITE;
}

static int __ag_math_isfinite_double(double value) {
  int classification = __ag_math_fpclassify_double(value);
  return classification != FP_NAN && classification != FP_INFINITE;
}

static int __ag_math_isfinite_long_double(long double value) {
  return __ag_math_isfinite_double((double)value);
}

static int __ag_math_signbit_float(float value) {
  union __ag_math_float_bits repr;
  repr.value = value;
  return (int)(repr.bits >> 31);
}

static int __ag_math_signbit_double(double value) {
  union __ag_math_double_bits repr;
  repr.value = value;
  return (int)(repr.bits >> 63);
}

static int __ag_math_signbit_long_double(long double value) {
  return __ag_math_signbit_double((double)value);
}

static int __ag_math_isgreater(double x, double y) { return x > y; }
static int __ag_math_isgreaterequal(double x, double y) { return x >= y; }
static int __ag_math_isless(double x, double y) { return x < y; }
static int __ag_math_islessequal(double x, double y) { return x <= y; }
static int __ag_math_islessgreater(double x, double y) {
  return x < y || x > y;
}
static int __ag_math_isunordered(double x, double y) {
  return __ag_math_fpclassify_double(x) == FP_NAN ||
         __ag_math_fpclassify_double(y) == FP_NAN;
}

#define __ag_math_classify(x) \
  _Generic((x), \
           float: __ag_math_fpclassify_float, \
           long double: __ag_math_fpclassify_long_double, \
           default: __ag_math_fpclassify_double)(x)
#define __ag_math_sign(x) \
  _Generic((x), \
           float: __ag_math_signbit_float, \
           long double: __ag_math_signbit_long_double, \
           default: __ag_math_signbit_double)(x)

#define fpclassify(x) __ag_math_classify(x)
#define isfinite(x) \
  _Generic((x), \
           float: __ag_math_isfinite_float, \
           long double: __ag_math_isfinite_long_double, \
           default: __ag_math_isfinite_double)(x)
#define isinf(x) (__ag_math_classify(x) == FP_INFINITE)
#define isnan(x) (__ag_math_classify(x) == FP_NAN)
#define isnormal(x) (__ag_math_classify(x) == FP_NORMAL)
#define signbit(x) __ag_math_sign(x)
#define isgreater(x, y) __ag_math_isgreater((double)(x), (double)(y))
#define isgreaterequal(x, y) \
  __ag_math_isgreaterequal((double)(x), (double)(y))
#define isless(x, y) __ag_math_isless((double)(x), (double)(y))
#define islessequal(x, y) __ag_math_islessequal((double)(x), (double)(y))
#define islessgreater(x, y) \
  __ag_math_islessgreater((double)(x), (double)(y))
#define isunordered(x, y) __ag_math_isunordered((double)(x), (double)(y))

#endif
