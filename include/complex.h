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

/* cabs(z) = sqrt(Re^2 + Im^2)。 */
#define cabs(z)  (sqrt((double)(__real__ (z)) * (double)(__real__ (z)) + \
                       (double)(__imag__ (z)) * (double)(__imag__ (z))))
#define cabsf(z) ((float)cabs(z))
#define cabsl(z) cabs(z)

/* carg(z) = atan2(Im, Re)。 */
#define carg(z)  (atan2((double)(__imag__ (z)), (double)(__real__ (z))))
#define cargf(z) ((float)carg(z))
#define cargl(z) carg(z)

/* 複素数を返す初等関数。_Complex の値渡し/値返しが効くので static 関数として
 * 実装する (引数 z は 1 回だけ評価される)。実部/虚部の計算は <math.h> の実数関数。
 * float/long double 版は double 版へ委譲する (引数・戻り値の暗黙変換)。
 * 標準ライブラリの同名関数を static 定義で隠すため、ag_c でも clang (-I で本ヘッダ
 * 使用) でも同一実装になり結果が一致する。 */

static double _Complex cexp(double _Complex z) {
  double r = exp(__real__ z);
  return (double _Complex){ r * cos(__imag__ z), r * sin(__imag__ z) };
}
static double _Complex clog(double _Complex z) {
  double re = __real__ z, im = __imag__ z;
  return (double _Complex){ log(sqrt(re * re + im * im)), atan2(im, re) };
}
static double _Complex csqrt(double _Complex z) {
  double re = __real__ z, im = __imag__ z;
  double m = sqrt(re * re + im * im);
  double tr = (m + re) * 0.5; if (tr < 0) tr = 0;
  double ti = (m - re) * 0.5; if (ti < 0) ti = 0;
  double rr = sqrt(tr), ri = sqrt(ti);
  if (im < 0) ri = -ri;
  return (double _Complex){ rr, ri };
}
static double _Complex cpow(double _Complex z, double _Complex w) {
  return cexp(w * clog(z));
}
static double _Complex csin(double _Complex z) {
  double re = __real__ z, im = __imag__ z;
  return (double _Complex){ sin(re) * cosh(im), cos(re) * sinh(im) };
}
static double _Complex ccos(double _Complex z) {
  double re = __real__ z, im = __imag__ z;
  return (double _Complex){ cos(re) * cosh(im), -sin(re) * sinh(im) };
}
static double _Complex csinh(double _Complex z) {
  double re = __real__ z, im = __imag__ z;
  return (double _Complex){ sinh(re) * cos(im), cosh(re) * sin(im) };
}
static double _Complex ccosh(double _Complex z) {
  double re = __real__ z, im = __imag__ z;
  return (double _Complex){ cosh(re) * cos(im), sinh(re) * sin(im) };
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
