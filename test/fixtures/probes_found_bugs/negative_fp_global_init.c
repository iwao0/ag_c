// 負値の浮動小数点グローバル初期化子は IEEE ビットパターン (符号ビット込み) を
// .data に出力する必要がある。`-1.0f` は ND_FNEG(1.0f) だが psx_eval_const_fp が
// ND_FNEG を扱わず定数畳み込みに失敗し、スカラは has_init が立たず .comm(BSS=0)、
// 配列要素は ND_NUM 以外として 0 のままになっていた。ND_FNEG を畳み込み、fp 配列の
// 非 ND_NUM 要素も psx_eval_const_fp で評価して修正。
#include <assert.h>

float  gf = -1.0f;
double gd = -3.5;
double gexpr = -1.5 - 0.5;           // 定数式 = -2.0
float  fa[3] = {-1.0f, 2.5f, -0.5f}; // 負/正混在の配列
double da[2] = {-2.5, 4.0};
float  gp = 1.0f;                    // 正値 (退行確認)

static int near(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 1e-6; }

int main(void) {
    assert(near(gf, -1.0));
    assert(near(gd, -3.5));
    assert(near(gexpr, -2.0));
    assert(near(fa[0], -1.0));
    assert(near(fa[1], 2.5));
    assert(near(fa[2], -0.5));
    assert(near(da[0], -2.5));
    assert(near(da[1], 4.0));
    assert(near(gp, 1.0));
    return 0;
}
