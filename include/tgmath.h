#ifndef _TGMATH_H
#define _TGMATH_H
#include <math.h>
/* C11 7.25: 型総称マクロ。引数型 (float/double/long double) で f/無印/l 版へ
 * _Generic ディスパッチする。Apple ARM64 では long double==double (同一 ABI) なので
 * l 版呼び出しも安全。complex 版は対象外 (実数のみ)。下で必要な f/l 版を宣言する
 * (math.h は double 中心で f/l を網羅しないため)。 */
float       sqrtf(float);       long double sqrtl(long double);
float       cbrtf(float);       long double cbrtl(long double);
float       sinf(float);        long double sinl(long double);
float       cosf(float);        long double cosl(long double);
float       tanf(float);        long double tanl(long double);
float       asinf(float);       long double asinl(long double);
float       acosf(float);       long double acosl(long double);
float       atanf(float);       long double atanl(long double);
float       expf(float);        long double expl(long double);
float       exp2f(float);       long double exp2l(long double);
float       expm1f(float);      long double expm1l(long double);
float       logf(float);        long double logl(long double);
float       log1pf(float);      long double log1pl(long double);
float       log10f(float);      long double log10l(long double);
float       log2f(float);       long double log2l(long double);
float       floorf(float);      long double floorl(long double);
float       ceilf(float);       long double ceill(long double);
float       roundf(float);      long double roundl(long double);
float       truncf(float);      long double truncl(long double);
float       fabsf(float);       long double fabsl(long double);
float       powf(float, float); long double powl(long double, long double);
float       fmodf(float, float);long double fmodl(long double, long double);
float       remainderf(float, float);long double remainderl(long double, long double);
float       remquof(float, float, int *);long double remquol(long double, long double, int *);
float       fdimf(float, float);long double fdiml(long double, long double);
float       fmaf(float, float, float);long double fmal(long double, long double, long double);
float       frexpf(float, int *);long double frexpl(long double, int *);
float       ldexpf(float, int);  long double ldexpl(long double, int);
float       modff(float, float *);long double modfl(long double, long double *);
float       copysignf(float, float);long double copysignl(long double, long double);
float       atan2f(float, float);long double atan2l(long double, long double);
float       hypotf(float, float);long double hypotl(long double, long double);
float       fminf(float, float);long double fminl(long double, long double);
float       fmaxf(float, float);long double fmaxl(long double, long double);

/* 注: マクロ引数名は `fn`。`f` にすると float サフィックス `f` とトークン貼り付けで
 * 区別できず `f##f` が `sqrtsqrt` のように壊れる (引数名 == 貼り付け先トークンの衝突)。 */
#define __tg_un(fn, x)      _Generic((x), float: fn##f, long double: fn##l, default: fn)(x)
#define __tg_bin(fn, x, y)  _Generic((x), float: fn##f, long double: fn##l, default: fn)((x), (y))
#define __tg_tri(fn, x, y, z)  _Generic((x), float: fn##f, long double: fn##l, default: fn)((x), (y), (z))

#define sqrt(x)  __tg_un(sqrt, x)
#define cbrt(x)  __tg_un(cbrt, x)
#define sin(x)   __tg_un(sin, x)
#define cos(x)   __tg_un(cos, x)
#define tan(x)   __tg_un(tan, x)
#define asin(x)  __tg_un(asin, x)
#define acos(x)  __tg_un(acos, x)
#define atan(x)  __tg_un(atan, x)
#define exp(x)   __tg_un(exp, x)
#define exp2(x)  __tg_un(exp2, x)
#define expm1(x) __tg_un(expm1, x)
#define log(x)   __tg_un(log, x)
#define log1p(x) __tg_un(log1p, x)
#define log10(x) __tg_un(log10, x)
#define log2(x)  __tg_un(log2, x)
#define floor(x) __tg_un(floor, x)
#define ceil(x)  __tg_un(ceil, x)
#define round(x) __tg_un(round, x)
#define trunc(x) __tg_un(trunc, x)
#define fabs(x)  __tg_un(fabs, x)
#define pow(x, y)   __tg_bin(pow, x, y)
#define fmod(x, y)  __tg_bin(fmod, x, y)
#define remainder(x, y) __tg_bin(remainder, x, y)
#define remquo(x, y, z) __tg_tri(remquo, x, y, z)
#define fdim(x, y)  __tg_bin(fdim, x, y)
#define fma(x, y, z) __tg_tri(fma, x, y, z)
#define frexp(x, y) __tg_bin(frexp, x, y)
#define ldexp(x, y) __tg_bin(ldexp, x, y)
#define modf(x, y)  __tg_bin(modf, x, y)
#define copysign(x, y) __tg_bin(copysign, x, y)
#define atan2(x, y) __tg_bin(atan2, x, y)
#define hypot(x, y) __tg_bin(hypot, x, y)
#define fmin(x, y)  __tg_bin(fmin, x, y)
#define fmax(x, y)  __tg_bin(fmax, x, y)
#endif
