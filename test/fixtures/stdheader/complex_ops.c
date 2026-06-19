// 同梱 <complex.h> の基本機能を検査する。複素数算術はネイティブ、I は compound
// literal {0,1}、creal/cimag/conj はメモリレイアウト経由マクロ (lvalue 引数)。
#include <complex.h>
#include <assert.h>

static int dn(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 1e-9; }

int main(void) {
    // 虚数単位 I で複素数を構築
    double complex z = 3.0 + 4.0 * I;
    assert(dn(creal(z), 3.0));
    assert(dn(cimag(z), 4.0));

    // 加算
    double complex a = 1.0 + 2.0 * I;
    double complex b = 5.0 + 6.0 * I;
    double complex s = a + b;          // 6 + 8i
    assert(dn(creal(s), 6.0));
    assert(dn(cimag(s), 8.0));

    // 乗算: (1+2i)(3+4i) = -5 + 10i
    double complex c = (1.0 + 2.0 * I) * (3.0 + 4.0 * I);
    assert(dn(creal(c), -5.0));
    assert(dn(cimag(c), 10.0));

    // 共役
    double complex cj = conj(z);       // 3 - 4i
    assert(dn(creal(cj), 3.0));
    assert(dn(cimag(cj), -4.0));

    // 減算・除算
    double complex d = b - a;          // 4 + 4i
    assert(dn(creal(d), 4.0));
    assert(dn(cimag(d), 4.0));
    double complex q = (2.0 + 0.0 * I) / (1.0 + 0.0 * I);  // 2
    assert(dn(creal(q), 2.0));

    // brace 初期化
    double complex w = {7.0, 9.0};
    assert(dn(creal(w), 7.0));
    assert(dn(cimag(w), 9.0));

    // float complex (brace 初期化。double の I との混在変換は別経路なので使わない)
    float complex fz = {1.5f, 2.5f};
    assert(crealf(fz) > 1.49f && crealf(fz) < 1.51f);
    assert(cimagf(fz) > 2.49f && cimagf(fz) < 2.51f);

    // cproj は有限値で恒等 (creal/cimag 保存) — 変数経由で確認
    double complex p = cproj(z);
    assert(dn(creal(p), 3.0));
    assert(dn(cimag(p), 4.0));

    // creal/cimag が rvalue (式) に直接効く (__real__/__imag__ ベース)
    assert(dn(creal(a + b), 6.0));
    assert(dn(cimag(a + b), 8.0));
    assert(dn(creal((1.0 + 2.0 * I) * (3.0 + 4.0 * I)), -5.0));
    assert(dn(cimag((1.0 + 2.0 * I) * (3.0 + 4.0 * I)), 10.0));
    assert(dn(creal(conj(z)), 3.0));
    assert(dn(cimag(conj(z)), -4.0));
    return 0;
}
