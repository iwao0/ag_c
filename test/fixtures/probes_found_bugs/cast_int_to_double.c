// 整数→浮動小数点の明示キャスト `(double)i` が変換命令を出さないバグ。
// キャスト処理は operand->fp_kind を double に立てるだけで int→fp 変換 (I2F) を
// 挟まないため、整数のビットがそのまま double として解釈され値が壊れていた
// (例: (double)7 が ~0 になる)。`(double)a / b` 等は二項演算子側の変換に救われて
// いたため露見しにくかった。
// 修正前: exit=0
// 期待: exit=35  ((double)7 / 2 = 3.5, *10 = 35)
#include <assert.h>
int main(void) {
    int a = 7, b = 2;
    double r = (double)a / b;
    int via_div = (int)(r * 10);   // 35

    double d = (double)5;          // 直接の int->double キャスト
    int via_cast = (int)(d * 3);   // 15

    assert(via_div == 35);    // (double)7 / 2 * 10
    assert(via_cast == 15);   // (double)5 * 3
    return 0;
}
