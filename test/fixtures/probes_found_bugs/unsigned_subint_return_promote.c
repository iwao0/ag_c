// unsigned char/short を返す関数の結果は整数昇格 (C11 6.3.1.1) で signed int に
// なるため、比較は符号付きで行う。build_unqualified_call が unsigned 戻り型すべてに
// funcall.is_unsigned=1 を立てていたため、`unsigned char f(); f() > -1` が unsigned
// 比較になり -1 が UINT_MAX 扱いで誤って false になっていた。char/short は除外する。
// unsigned int/long は昇格しないので unsigned 比較を維持する。
#include <assert.h>

unsigned char  uc(void) { return 5; }
unsigned short us(void) { return 5; }
unsigned int   ui(void) { return 5; }

int main(void) {
    // unsigned char/short 戻り → signed int へ昇格 → -1 より大きい
    assert(uc() > -1);
    assert(us() > -1);

    // 値そのものの正しさ (ゼロ拡張) も維持
    assert(uc() == 5);
    assert(us() == 5);

    // unsigned int 戻りは unsigned 比較を維持: 5 > -1 は -1==UINT_MAX なので false
    int ui_gt = (ui() > -1);   // unsigned 比較なので 0
    assert(ui_gt == 0);
    assert(ui() == 5u);
    return 0;
}
