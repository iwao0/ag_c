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
 * 効く (例: `creal(a+b)`)。cabs/carg/cexp など <math.h> の実数関数を要するものは
 * 提供しない。_Complex の値渡し ABI は未実装 (関数引数で複素数を値で渡さないこと)。 */

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

#endif /* _COMPLEX_H */
