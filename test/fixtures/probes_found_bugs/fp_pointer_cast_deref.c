// `*(double*)ptr` / `((double*)ptr)[i]` のように、ポインタ型の式を float/double
// ポインタへキャストして deref/添字すると、要素が double でなく 8 バイト整数として
// ロードされ値が化けていた (apply_cast の単段ポインタキャスト分岐が deref_size は
// 更新するが pointee_fp_kind を立てていなかった)。pointee_fp_kind を立てて fp load
// にする。複素数のメモリレイアウト経由アクセスにも必要。
#include <assert.h>

static int dn(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 1e-9; }

int main(void) {
    double a[3] = {1.0, 2.5, 3.0};
    double *p = a;
    // ポインタ算術後のキャスト deref
    assert(dn(*(double *)(p + 1), 2.5));
    // 添字
    assert(dn(((double *)p)[2], 3.0));

    // char バッファを double* にキャストして読み書き
    char buf[16];
    double *dp = (double *)buf;
    *dp = 7.5;
    assert(dn(*(double *)buf, 7.5));

    // float 版
    float fa[2] = {1.5f, 2.5f};
    float *fp = fa;
    float fr = *(float *)(fp + 1);
    assert(fr > 2.49f && fr < 2.51f);
    return 0;
}
