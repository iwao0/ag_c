// _Complex の値渡し / 値返し ABI (AAPCS64 / Apple ARM64)。複素数は 2 要素の同型
// 浮動小数集合体 (HFA) として、re→d0/s0, im→d1/s1 の連続 FP レジスタペアで渡し、
// 戻り値も同様。以前は double complex がポインタ渡し扱い、float complex も 1 レジスタ
// のみで虚部が落ちていた。param 受取・return・呼び出し引数・戻り値受取の 4 経路を実装。
#include <assert.h>

static int dn(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 1e-9; }

static double getre(double _Complex z) { return __real__ z; }
static double getim(double _Complex z) { return __imag__ z; }
static double _Complex addc(double _Complex a, double _Complex b) { return a + b; }
static double _Complex mulc(double _Complex a, double _Complex b) { return a * b; }
static float _Complex addcf(float _Complex a, float _Complex b) { return a + b; }
// 整数/double と複素数の混在引数 (FP/整数レジスタは独立カウンタ)
static double mix(int n, double _Complex z, double d) {
    return n + __real__ z + __imag__ z + d;
}

int main(void) {
    double _Complex z = {5.0, 7.0};
    // 値渡し (param 受取)
    assert(dn(getre(z), 5.0));
    assert(dn(getim(z), 7.0));

    // 値渡し + 値返し
    double _Complex a = {1.0, 2.0}, b = {3.0, 4.0};
    double _Complex s = addc(a, b);          // 4 + 6i
    assert(dn(__real__ s, 4.0));
    assert(dn(__imag__ s, 6.0));

    double _Complex p = mulc(a, b);          // (1+2i)(3+4i) = -5 + 10i
    assert(dn(__real__ p, -5.0));
    assert(dn(__imag__ p, 10.0));

    // 連鎖呼び出し (戻り値をそのまま引数へ)
    double _Complex w = {10.0, 20.0};
    double _Complex c = addc(addc(a, b), w); // 14 + 26i
    assert(dn(__real__ c, 14.0));
    assert(dn(__imag__ c, 26.0));

    // 戻り値を直接 __real__/__imag__
    assert(dn(__real__(addc(a, b)), 4.0));
    assert(dn(__imag__(mulc(a, b)), 10.0));

    // float complex 版
    float _Complex fa = {1.5f, 2.5f}, fb = {0.5f, 0.5f};
    float _Complex fs = addcf(fa, fb);       // 2.0 + 3.0i
    assert(((float *)&fs)[0] > 1.99f && ((float *)&fs)[0] < 2.01f);
    assert(((float *)&fs)[1] > 2.99f && ((float *)&fs)[1] < 3.01f);

    // 整数/double 混在引数
    assert(dn(mix(10, z, 1.5), 10.0 + 5.0 + 7.0 + 1.5));
    return 0;
}
