// GNU 拡張 __real__ / __imag__: 複素数の実部/虚部を取り出す単項演算子。
// 複素数式 (rvalue 含む) を temp slot に materialize して re[0]/im[half] を fp load
// する。実数オペランドでは __real__ x = x, __imag__ x = 0。complex.h の creal/cimag
// を rvalue にも効かせるための基盤。キーワードでなく特殊識別子として扱う。
#include <assert.h>

static int dn(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 1e-9; }

int main(void) {
    // lvalue 複素数
    double _Complex z = {3.0, 4.0};
    assert(dn(__real__ z, 3.0));
    assert(dn(__imag__ z, 4.0));

    // rvalue 複素数式 (加算/乗算)
    double _Complex a = {1.0, 2.0}, b = {3.0, 4.0};
    assert(dn(__real__(a + b), 4.0));
    assert(dn(__imag__(a + b), 6.0));
    assert(dn(__real__(a * b), -5.0));   // (1+2i)(3+4i) = -5+10i
    assert(dn(__imag__(a * b), 10.0));

    // 実数スカラ
    double x = 9.0;
    assert(dn(__real__ x, 9.0));
    assert(dn(__imag__ x, 0.0));

    // float 複素数
    float _Complex fz = {1.5f, 2.5f};
    float fr = __real__ fz, fi = __imag__ fz;
    assert(fr > 1.49f && fr < 1.51f);
    assert(fi > 2.49f && fi < 2.51f);
    return 0;
}
