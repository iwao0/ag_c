#ifndef _TGMATH_H
#define _TGMATH_H
#include <math.h>
#include <complex.h>
/* C11 7.25: 型総称マクロ。実数の総称引数を共通の実数型へまとめて
 * f/無印/l 版へ _Generic ディスパッチする。整数引数は double として扱う。
 * Apple ARM64 では long double==double (同一 ABI) なので l 版呼び出しも安全。
 * complex 対応関数は引数のdomainもまとめ、c/無印/l 版へ振り分ける。
 * 下で必要な f/l 版を宣言する (math.h は double 中心で f/l を網羅しないため)。 */
float       sqrtf(float);       long double sqrtl(long double);
float       cbrtf(float);       long double cbrtl(long double);
float       sinf(float);        long double sinl(long double);
float       cosf(float);        long double cosl(long double);
float       tanf(float);        long double tanl(long double);
float       sinhf(float);       long double sinhl(long double);
float       coshf(float);       long double coshl(long double);
float       tanhf(float);       long double tanhl(long double);
float       asinhf(float);      long double asinhl(long double);
float       acoshf(float);      long double acoshl(long double);
float       atanhf(float);      long double atanhl(long double);
float       asinf(float);       long double asinl(long double);
float       acosf(float);       long double acosl(long double);
float       atanf(float);       long double atanl(long double);
float       expf(float);        long double expl(long double);
float       exp2f(float);       long double exp2l(long double);
float       expm1f(float);      long double expm1l(long double);
float       erff(float);        long double erfl(long double);
float       erfcf(float);       long double erfcl(long double);
float       logf(float);        long double logl(long double);
float       log1pf(float);      long double log1pl(long double);
float       log10f(float);      long double log10l(long double);
float       log2f(float);       long double log2l(long double);
float       floorf(float);      long double floorl(long double);
float       ceilf(float);       long double ceill(long double);
float       roundf(float);      long double roundl(long double);
float       truncf(float);      long double truncl(long double);
float       nearbyintf(float);  long double nearbyintl(long double);
float       rintf(float);       long double rintl(long double);
long        lrintf(float);      long lrintl(long double);
long long   llrintf(float);     long long llrintl(long double);
long        lroundf(float);     long lroundl(long double);
long long   llroundf(float);    long long llroundl(long double);
float       fabsf(float);       long double fabsl(long double);
float       powf(float, float); long double powl(long double, long double);
float       fmodf(float, float);long double fmodl(long double, long double);
float       remainderf(float, float);long double remainderl(long double, long double);
float       remquof(float, float, int *);long double remquol(long double, long double, int *);
float       fdimf(float, float);long double fdiml(long double, long double);
float       fmaf(float, float, float);long double fmal(long double, long double, long double);
float       frexpf(float, int *);long double frexpl(long double, int *);
float       ldexpf(float, int);  long double ldexpl(long double, int);
float       scalbnf(float, int); long double scalbnl(long double, int);
float       scalblnf(float, long);long double scalblnl(long double, long);
int         ilogbf(float);       int ilogbl(long double);
float       logbf(float);        long double logbl(long double);
float       modff(float, float *);long double modfl(long double, long double *);
float       copysignf(float, float);long double copysignl(long double, long double);
float       atan2f(float, float);long double atan2l(long double, long double);
float       hypotf(float, float);long double hypotl(long double, long double);
float       fminf(float, float);long double fminl(long double, long double);
float       fmaxf(float, float);long double fmaxl(long double, long double);

/* 注: マクロ引数名は `fn`。`f` にすると float サフィックス `f` とトークン貼り付けで
 * 区別できず `f##f` が `sqrtsqrt` のように壊れる (引数名 == 貼り付け先トークンの衝突)。 */
#define __tg_real_type(x) \
  _Generic((x), float: (float)0, long double: (long double)0, default: (double)0)
#define __tg_math_type(x) \
  _Generic((x), \
           float: (float)0, \
           long double: (long double)0, \
           float _Complex: (float _Complex)0, \
           double _Complex: (double _Complex)0, \
           long double _Complex: (long double _Complex)0, \
           default: (double)0)
#define __tg_un(fn, x) \
  _Generic((x), float: fn##f, long double: fn##l, default: fn)(x)
#define __tg_un_real_or_complex(real_fn, complex_fn, x) \
  _Generic((x), \
           float: real_fn##f, \
           long double: real_fn##l, \
           float _Complex: complex_fn##f, \
           double _Complex: complex_fn, \
           long double _Complex: complex_fn##l, \
           default: real_fn)(x)
#define __tg_un_complex(fn, x) \
  _Generic((x), \
           float: fn##f((float _Complex)(x)), \
           long double: fn##l((long double _Complex)(x)), \
           float _Complex: fn##f((float _Complex)(x)), \
           double _Complex: fn((double _Complex)(x)), \
           long double _Complex: fn##l((long double _Complex)(x)), \
           default: fn((double _Complex)(x)))
#define __tg_bin(fn, x, y) \
  _Generic((__tg_real_type(x) + __tg_real_type(y)), \
           float: fn##f, long double: fn##l, default: fn)((x), (y))
#define __tg_bin_real_or_complex(real_fn, complex_fn, x, y) \
  _Generic((__tg_math_type(x) + __tg_math_type(y)), \
           float: real_fn##f((float)(x), (float)(y)), \
           long double: real_fn##l((long double)(x), (long double)(y)), \
           float _Complex: complex_fn##f( \
               (float _Complex)(x), (float _Complex)(y)), \
           double _Complex: complex_fn( \
               (double _Complex)(x), (double _Complex)(y)), \
           long double _Complex: complex_fn##l( \
               (long double _Complex)(x), (long double _Complex)(y)), \
           default: real_fn((double)(x), (double)(y)))
#define __tg_tri(fn, x, y, z) \
  _Generic((__tg_real_type(x) + __tg_real_type(y) + __tg_real_type(z)), \
           float: fn##f, long double: fn##l, default: fn)((x), (y), (z))
#define __tg_first_bin(fn, x, y) \
  _Generic((x), float: fn##f, long double: fn##l, default: fn)((x), (y))
#define __tg_bin_out(fn, x, y, z) \
  _Generic((__tg_real_type(x) + __tg_real_type(y)), \
           float: fn##f, long double: fn##l, default: fn)((x), (y), (z))

#define sqrt(x)  __tg_un_real_or_complex(sqrt, csqrt, x)
#define cbrt(x)  __tg_un(cbrt, x)
#define sin(x)   __tg_un_real_or_complex(sin, csin, x)
#define cos(x)   __tg_un_real_or_complex(cos, ccos, x)
#define tan(x)   __tg_un_real_or_complex(tan, ctan, x)
#define sinh(x)  __tg_un_real_or_complex(sinh, csinh, x)
#define cosh(x)  __tg_un_real_or_complex(cosh, ccosh, x)
#define tanh(x)  __tg_un_real_or_complex(tanh, ctanh, x)
#define asinh(x) __tg_un_real_or_complex(asinh, casinh, x)
#define acosh(x) __tg_un_real_or_complex(acosh, cacosh, x)
#define atanh(x) __tg_un_real_or_complex(atanh, catanh, x)
#define asin(x)  __tg_un_real_or_complex(asin, casin, x)
#define acos(x)  __tg_un_real_or_complex(acos, cacos, x)
#define atan(x)  __tg_un_real_or_complex(atan, catan, x)
#define exp(x)   __tg_un_real_or_complex(exp, cexp, x)
#define exp2(x)  __tg_un(exp2, x)
#define expm1(x) __tg_un(expm1, x)
#define erf(x)   __tg_un(erf, x)
#define erfc(x)  __tg_un(erfc, x)
#define log(x)   __tg_un_real_or_complex(log, clog, x)
#define log1p(x) __tg_un(log1p, x)
#define log10(x) __tg_un(log10, x)
#define log2(x)  __tg_un(log2, x)
#define floor(x) __tg_un(floor, x)
#define ceil(x)  __tg_un(ceil, x)
#define round(x) __tg_un(round, x)
#define trunc(x) __tg_un(trunc, x)
#define nearbyint(x) __tg_un(nearbyint, x)
#define rint(x)  __tg_un(rint, x)
#define lrint(x) __tg_un(lrint, x)
#define llrint(x) __tg_un(llrint, x)
#define lround(x) __tg_un(lround, x)
#define llround(x) __tg_un(llround, x)
#define fabs(x)  __tg_un_real_or_complex(fabs, cabs, x)
#define pow(x, y)   __tg_bin_real_or_complex(pow, cpow, x, y)
#define fmod(x, y)  __tg_bin(fmod, x, y)
#define remainder(x, y) __tg_bin(remainder, x, y)
#define remquo(x, y, z) __tg_bin_out(remquo, x, y, z)
#define fdim(x, y)  __tg_bin(fdim, x, y)
#define fma(x, y, z) __tg_tri(fma, x, y, z)
#define frexp(x, y) __tg_first_bin(frexp, x, y)
#define ldexp(x, y) __tg_first_bin(ldexp, x, y)
#define scalbn(x, y) __tg_first_bin(scalbn, x, y)
#define scalbln(x, y) __tg_first_bin(scalbln, x, y)
#define ilogb(x) __tg_un(ilogb, x)
#define logb(x) __tg_un(logb, x)
#define modf(x, y)  __tg_first_bin(modf, x, y)
#define copysign(x, y) __tg_bin(copysign, x, y)
#define atan2(x, y) __tg_bin(atan2, x, y)
#define hypot(x, y) __tg_bin(hypot, x, y)
#define fmin(x, y)  __tg_bin(fmin, x, y)
#define fmax(x, y)  __tg_bin(fmax, x, y)
#define nextafter(x, y) __tg_bin(nextafter, x, y)
#define nexttoward(x, y) __tg_first_bin(nexttoward, x, y)
#define tgamma(x) __tg_un(tgamma, x)
#define lgamma(x) __tg_un(lgamma, x)
#define carg(x)  __tg_un_complex(carg, x)
#define cimag(x) __tg_un_complex(cimag, x)
#define conj(x)  __tg_un_complex(conj, x)
#define cproj(x) __tg_un_complex(cproj, x)
#define creal(x) __tg_un_complex(creal, x)
#endif
