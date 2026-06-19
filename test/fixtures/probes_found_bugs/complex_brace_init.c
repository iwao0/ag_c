// `_Complex z = {re, im}` の brace 初期化。複素数は {実部, 虚部} 連続レイアウトなので
// 実部スロットと虚部スロットへ fp スカラ store を生成する。従来はスカラ初期化子扱いで
// 2 要素 `{0,1}` が E3064 になり、虚数単位 (= {0,1}) を作れなかった。
// (complex.h を機能させる足がかり。複素数算術自体は元から動作。)
#include <assert.h>

static int dnear(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 1e-9; }

int main(void) {
    double _Complex z = {3.0, 4.0};
    double *pz = (double *)&z;
    assert(dnear(pz[0], 3.0));   // 実部
    assert(dnear(pz[1], 4.0));   // 虚部

    // 虚数単位 i = {0,1} を作り、a + b*i が複素数算術で組み立てられる
    double _Complex i = {0, 1};
    double _Complex w = 5.0 + 2.0 * i;   // 5 + 2i
    double *pw = (double *)&w;
    assert(dnear(pw[0], 5.0));
    assert(dnear(pw[1], 2.0));

    // 虚部省略は 0
    double _Complex r = {7.0};
    double *pr = (double *)&r;
    assert(dnear(pr[0], 7.0));
    assert(dnear(pr[1], 0.0));

    // float _Complex
    float _Complex fz = {1.5f, 2.5f};
    float *pf = (float *)&fz;
    assert(pf[0] > 1.49f && pf[0] < 1.51f);
    assert(pf[1] > 2.49f && pf[1] < 2.51f);

    // 複素数乗算 (既存機能の確認): (1+2i)(3+4i) = -5+10i
    double _Complex a = {1, 2}, b = {3, 4};
    double _Complex c = a * b;
    double *pc = (double *)&c;
    assert(dnear(pc[0], -5.0));
    assert(dnear(pc[1], 10.0));
    return 0;
}
