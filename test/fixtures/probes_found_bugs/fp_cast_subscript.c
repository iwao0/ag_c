// `((double*)p)[i]` / `((double*)&x)[i]` のように float/double ポインタへキャストして
// 添字する形。apply_cast の fp ポインタキャスト分岐が base_deref_size を立てており、
// 添字機構が「要素自体がポインタ」と誤解して結果をポインタ扱い (E3064) していた。
// fp ポインタの要素はスカラなので base_deref_size を立てないようにして修正。
// (複素数の実部/虚部にメモリレイアウト経由でアクセスする creal/cimag に必要。)
#include <assert.h>

static int dn(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 1e-9; }

int main(void) {
    // double _Complex の実部/虚部を (double*) 経由で読む (creal/cimag 相当)
    double _Complex z = {3.0, 4.0};
    assert(dn(((double *)&z)[0], 3.0));   // creal
    assert(dn(((double *)&z)[1], 4.0));   // cimag

    // float _Complex
    float _Complex fz = {1.5f, 2.5f};
    assert(((float *)&fz)[0] > 1.49f && ((float *)&fz)[0] < 1.51f);
    assert(((float *)&fz)[1] > 2.49f && ((float *)&fz)[1] < 2.51f);

    // 通常の double スカラ/配列への (double*) キャスト添字
    double d = 7.5;
    assert(dn(((double *)&d)[0], 7.5));
    double a[3] = {1.0, 2.0, 3.0};
    assert(dn(((double *)a)[2], 3.0));
    return 0;
}
