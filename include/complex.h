#ifndef _COMPLEX_H
#define _COMPLEX_H

/* ag_c 同梱 <complex.h> (C11 7.3)。
 *
 * ag_c の _Complex は {実部, 虚部} の連続レイアウト (double _Complex = 16B,
 * float _Complex = 8B) で、複素数の四則演算 (+ - * /) はコンパイラがネイティブに
 * 行う。虚数単位 I は複素数 compound literal {0,1} として定義し、`a + b*I` が
 * 複素数算術で組み立てられる。
 *
 * creal/cimag は GNU 拡張 __real__/__imag__ で実装するため任意の式 (rvalue) に
 * 効く (例: `creal(a+b)`)。cabs/carg は <math.h> の sqrt/atan2 経由。_Complex の
 * 値渡し / 値返し (AAPCS64 HFA: re→d0, im→d1) も実装済みなので、複素数を値で
 * 受け取る / 返す関数を自分で書ける。cexp/clog/csqrt 等の複素数 math 関数は未提供。 */

#define complex   _Complex
#define imaginary _Imaginary

/* 虚数単位。_Complex_I は i (= 0 + 1i)。I は通常 _Complex_I。 */
#define _Complex_I   ((double _Complex){0.0, 1.0})
#define _Imaginary_I ((double _Complex){0.0, 1.0})
#define I            _Complex_I

/* 実部・虚部の取り出し。__real__/__imag__ は任意の複素数式に効く
 * (実数オペランドでは __real__ x = x, __imag__ x = 0)。 */
#define creal(z)  (__real__ (z))
#define cimag(z)  (__imag__ (z))
#define crealf(z) (__real__ (z))
#define cimagf(z) (__imag__ (z))
#define creall(z) (__real__ (z))
#define cimagl(z) (__imag__ (z))

/* 複素共役 (虚部の符号反転)。新しい複素数を compound literal で構築する。 */
#define conj(z)  ((double _Complex){ __real__ (z), -__imag__ (z) })
#define conjf(z) ((float _Complex){ __real__ (z), -__imag__ (z) })
#define conjl(z) conj(z)

/* cproj: 無限遠点への射影。有限値ではそのまま (恒等)。 */
#define cproj(z)  (z)
#define cprojf(z) (z)
#define cprojl(z) (z)

/* 複素数の絶対値・偏角は <math.h> の実数関数で計算する (ag_c では sqrt/atan2 が
 * リンクする)。マクロ実装のため引数 z は __real__/__imag__ で複数回評価される点に
 * 注意 (純粋な複素数値・式なら副作用なしで問題ない)。 */
#include <math.h>

static double __ag_complex_sqrt(double x) {
  if (x <= 0.0) return 0.0;
  double r = x >= 1.0 ? x : 1.0;
  for (int i = 0; i < 32; i = i + 1) r = 0.5 * (r + x / r);
  return r;
}

static double __ag_complex_exp(double x) {
  double r = x;
  int k = 0;
  while (r > 0.5) { r = r - 0.6931471805599453; k = k + 1; }
  while (r < -0.5) { r = r + 0.6931471805599453; k = k - 1; }
  double term = 1.0;
  double sum = 1.0;
  double n = 1.0;
  for (int i = 0; i < 32; i = i + 1) {
    term = term * r / n;
    sum = sum + term;
    n = n + 1.0;
  }
  while (k > 0) { sum = sum * 2.0; k = k - 1; }
  while (k < 0) { sum = sum * 0.5; k = k + 1; }
  return sum;
}

static double __ag_complex_log(double x) {
  if (x <= 0.0) return -1.0 / 0.0;
  double z = (x - 1.0) / (x + 1.0);
  double z2 = z * z;
  double term = z;
  double sum = z;
  double den = 3.0;
  for (int i = 0; i < 96; i = i + 1) {
    term = term * z2;
    sum = sum + term / den;
    den = den + 2.0;
  }
  return 2.0 * sum;
}

static double __ag_complex_sin(double x) {
  while (x > 3.141592653589793) x = x - 6.283185307179586;
  while (x < -3.141592653589793) x = x + 6.283185307179586;
  if (x > 1.5707963267948966) x = 3.141592653589793 - x;
  if (x < -1.5707963267948966) x = -3.141592653589793 - x;
  double x2 = x * x;
  double term = x;
  double sum = x;
  for (int i = 1; i < 18; i = i + 1) {
    double den = (double)((i * 2) * (i * 2 + 1));
    term = -(term * x2) / den;
    sum = sum + term;
  }
  return sum;
}

static double __ag_complex_cos(double x) {
  double sign = 1.0;
  while (x > 3.141592653589793) x = x - 6.283185307179586;
  while (x < -3.141592653589793) x = x + 6.283185307179586;
  if (x > 1.5707963267948966) { x = 3.141592653589793 - x; sign = -1.0; }
  if (x < -1.5707963267948966) { x = -3.141592653589793 - x; sign = -1.0; }
  double x2 = x * x;
  double term = 1.0;
  double sum = 1.0;
  for (int i = 1; i < 18; i = i + 1) {
    double den = (double)((i * 2 - 1) * (i * 2));
    term = -(term * x2) / den;
    sum = sum + term;
  }
  return sign * sum;
}

static double __ag_complex_atan_core(double x) {
  double x2 = x * x;
  double term = x;
  double sum = x;
  double den = 3.0;
  int neg = 1;
  for (int i = 0; i < 96; i = i + 1) {
    term = term * x2;
    if (neg) sum = sum - term / den;
    else sum = sum + term / den;
    neg = !neg;
    den = den + 2.0;
  }
  return sum;
}

static double __ag_complex_atan(double x) {
  if (x > 1.0) return 1.5707963267948966 - __ag_complex_atan_core(1.0 / x);
  if (x < -1.0) return -1.5707963267948966 - __ag_complex_atan_core(1.0 / x);
  return __ag_complex_atan_core(x);
}

static double __ag_complex_atan2(double y, double x) {
  if (x > 0.0) return __ag_complex_atan(y / x);
  if (x < 0.0) {
    if (y >= 0.0) return __ag_complex_atan(y / x) + 3.141592653589793;
    return __ag_complex_atan(y / x) - 3.141592653589793;
  }
  if (y > 0.0) return 1.5707963267948966;
  if (y < 0.0) return -1.5707963267948966;
  return 0.0;
}

static double __ag_complex_sinh(double x) {
  return 0.5 * (__ag_complex_exp(x) - __ag_complex_exp(-x));
}

static double __ag_complex_cosh(double x) {
  return 0.5 * (__ag_complex_exp(x) + __ag_complex_exp(-x));
}

/* cabs(z) = sqrt(Re^2 + Im^2)。 */
#define cabs(z)  (__ag_complex_sqrt((double)(__real__ (z)) * (double)(__real__ (z)) + \
                                    (double)(__imag__ (z)) * (double)(__imag__ (z))))
#define cabsf(z) ((float)cabs(z))
#define cabsl(z) cabs(z)

/* carg(z) = atan2(Im, Re)。 */
#define carg(z)  (__ag_complex_atan2((double)(__imag__ (z)), (double)(__real__ (z))))
#define cargf(z) ((float)carg(z))
#define cargl(z) carg(z)

/* 複素数を返す初等関数。_Complex の値渡し/値返しが効くので static 関数として
 * 実装する (引数 z は 1 回だけ評価される)。実部/虚部の計算は <math.h> の実数関数。
 * float/long double 版は double 版へ委譲する (引数・戻り値の暗黙変換)。
 * 標準ライブラリの同名関数を static 定義で隠すため、ag_c でも clang (-I で本ヘッダ
 * 使用) でも同一実装になり結果が一致する。 */

static double _Complex cexp(double _Complex z) {
  double r = __ag_complex_exp(__real__ z);
  return (double _Complex){ r * __ag_complex_cos(__imag__ z), r * __ag_complex_sin(__imag__ z) };
}
static double _Complex clog(double _Complex z) {
  double re = __real__ z, im = __imag__ z;
  return (double _Complex){ __ag_complex_log(__ag_complex_sqrt(re * re + im * im)),
                            __ag_complex_atan2(im, re) };
}
static double _Complex csqrt(double _Complex z) {
  double re = __real__ z, im = __imag__ z;
  double m = __ag_complex_sqrt(re * re + im * im);
  double tr = (m + re) * 0.5; if (tr < 0) tr = 0;
  double ti = (m - re) * 0.5; if (ti < 0) ti = 0;
  double rr = __ag_complex_sqrt(tr), ri = __ag_complex_sqrt(ti);
  if (im < 0) ri = -ri;
  return (double _Complex){ rr, ri };
}
static double _Complex cpow(double _Complex z, double _Complex w) {
  return cexp(w * clog(z));
}
static double _Complex csin(double _Complex z) {
  double re = __real__ z, im = __imag__ z;
  return (double _Complex){ __ag_complex_sin(re) * __ag_complex_cosh(im),
                            __ag_complex_cos(re) * __ag_complex_sinh(im) };
}
static double _Complex ccos(double _Complex z) {
  double re = __real__ z, im = __imag__ z;
  return (double _Complex){ __ag_complex_cos(re) * __ag_complex_cosh(im),
                            -__ag_complex_sin(re) * __ag_complex_sinh(im) };
}
static double _Complex csinh(double _Complex z) {
  double re = __real__ z, im = __imag__ z;
  return (double _Complex){ __ag_complex_sinh(re) * __ag_complex_cos(im),
                            __ag_complex_cosh(re) * __ag_complex_sin(im) };
}
static double _Complex ccosh(double _Complex z) {
  double re = __real__ z, im = __imag__ z;
  return (double _Complex){ __ag_complex_cosh(re) * __ag_complex_cos(im),
                            __ag_complex_sinh(re) * __ag_complex_sin(im) };
}
static double _Complex ctan(double _Complex z)  { return csin(z) / ccos(z); }
static double _Complex ctanh(double _Complex z) { return csinh(z) / ccosh(z); }

/* float _Complex 版: double 版へ委譲 (引数昇格・戻り値変換)。 */
static float _Complex cexpf(float _Complex z)  { return cexp(z); }
static float _Complex clogf(float _Complex z)  { return clog(z); }
static float _Complex csqrtf(float _Complex z) { return csqrt(z); }
static float _Complex csinf(float _Complex z)  { return csin(z); }
static float _Complex ccosf(float _Complex z)  { return ccos(z); }

#endif /* _COMPLEX_H */
