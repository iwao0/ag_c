// double _Complex ↔ float _Complex の変換。build_complex_to が成分の fp 種別を
// 無視して 2*half バイトを単純 memcpy していたため、源と先で fp 種別が違うと
// ビット列が壊れて虚部が落ちていた。源の fp で materialize → 各成分を F2F 変換
// して格納するよう修正。初期化・代入・I 混在のいずれでも正しく変換される。
#include <assert.h>

static int dn(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 1e-5; }

int main(void) {
    // double complex → float complex (初期化)
    double _Complex d = {3.0, 4.0};
    float _Complex f = d;
    assert(((float *)&f)[0] > 2.99f && ((float *)&f)[0] < 3.01f);
    assert(((float *)&f)[1] > 3.99f && ((float *)&f)[1] < 4.01f);

    // float complex → double complex (初期化)
    float _Complex f2 = {5.0f, 6.0f};
    double _Complex d2 = f2;
    assert(dn(((double *)&d2)[0], 5.0));
    assert(dn(((double *)&d2)[1], 6.0));

    // 代入での変換
    double _Complex d3 = {1.0, 2.0};
    float _Complex f3 = {0.0f, 0.0f};
    f3 = d3;
    assert(((float *)&f3)[0] > 0.99f && ((float *)&f3)[0] < 1.01f);
    assert(((float *)&f3)[1] > 1.99f && ((float *)&f3)[1] < 2.01f);

    // double の虚数単位 I (= double complex {0,1}) を float complex に混在
    double _Complex Iu = {0.0, 1.0};
    float _Complex fz = 1.5f + 2.5f * Iu;   // 2.5f*Iu は double complex、和も double → float complex へ変換
    assert(((float *)&fz)[0] > 1.49f && ((float *)&fz)[0] < 1.51f);
    assert(((float *)&fz)[1] > 2.49f && ((float *)&fz)[1] < 2.51f);

    // 変換後も純 float complex 算術が正しい
    float _Complex a = f2 * f2;   // (5+6i)^2 = -11 + 60i
    assert(((float *)&a)[0] > -11.01f && ((float *)&a)[0] < -10.99f);
    assert(((float *)&a)[1] > 59.99f && ((float *)&a)[1] < 60.01f);
    return 0;
}
