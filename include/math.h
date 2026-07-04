#ifndef _MATH_H
#define _MATH_H

#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4

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

double exp(double x);
double exp2(double x);
double expm1(double x);
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

double fabs(double x);
double fmod(double x, double y);
double remainder(double x, double y);
double remquo(double x, double y, int *quo);
double fdim(double x, double y);
double fma(double x, double y, double z);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
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

#endif
