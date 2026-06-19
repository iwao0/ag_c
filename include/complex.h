#ifndef _COMPLEX_H
#define _COMPLEX_H

/* ag_c 同梱 <complex.h> (C11 7.3)。
 *
 * ag_c の _Complex は {実部, 虚部} の連続レイアウト (double _Complex = 16B,
 * float _Complex = 8B) で、複素数の四則演算 (+ - * /) はコンパイラがネイティブに
 * 行う。虚数単位 I は複素数 compound literal {0,1} として定義し、`a + b*I` が
 * 複素数算術で組み立てられる。
 *
 * 制約: ag_c には __real__/__imag__ や _Complex の値渡し ABI が無いため、
 * creal/cimag/conj 等はメモリレイアウト経由のマクロで実装している。引数は
 * アドレス取得可能 (lvalue) でなければならない (例: 変数。`creal(a+b)` のような
 * rvalue は一旦変数に代入してから渡すこと)。cabs/carg/cexp など <math.h> の
 * 実数関数を要するものは提供しない。 */

#define complex   _Complex
#define imaginary _Imaginary

/* 虚数単位。_Complex_I は i (= 0 + 1i)。I は通常 _Complex_I。 */
#define _Complex_I   ((double _Complex){0.0, 1.0})
#define _Imaginary_I ((double _Complex){0.0, 1.0})
#define I            _Complex_I

/* 実部・虚部の取り出し (引数は lvalue)。double/float/long double 版。
 * long double は本環境では double と同一レイアウト。 */
#define creal(z)  (((double *)&(z))[0])
#define cimag(z)  (((double *)&(z))[1])
#define crealf(z) (((float *)&(z))[0])
#define cimagf(z) (((float *)&(z))[1])
#define creall(z) (((double *)&(z))[0])
#define cimagl(z) (((double *)&(z))[1])

/* 複素共役 (虚部の符号反転)。新しい複素数を compound literal で構築する。 */
#define conj(z)  ((double _Complex){ ((double *)&(z))[0], -((double *)&(z))[1] })
#define conjf(z) ((float _Complex){ ((float *)&(z))[0], -((float *)&(z))[1] })
#define conjl(z) conj(z)

/* cproj: 無限遠点への射影。有限値ではそのまま (恒等)。 */
#define cproj(z)  (z)
#define cprojf(z) (z)
#define cprojl(z) (z)

#endif /* _COMPLEX_H */
