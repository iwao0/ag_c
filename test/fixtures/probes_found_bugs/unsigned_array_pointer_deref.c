// unsigned 配列要素 / unsigned ポインタ deref の直接使用 (比較等) が、符号拡張
// load され値が化けていたバグ。
// `unsigned a[2]; a[0] == 0xFFFFFFFFu` や `unsigned *p; *p == 0xFFFFFFFFu` が、
// 要素/pointee を ldrsw (sign-extend = -1) でロードし、定数 (zero-extend) と一致せず
// false になっていた。変数へ代入すると truncate で動いていた (直接使用のみ)。
// 原因: subscript / 単項 deref がcanonical pointee signednessを参照していなかった。
// 修正: canonical array/pointer typeのpointee型から結果ND_DEREFの型を構築する。
// 期待: exit=42
#include <assert.h>
unsigned garr[3] = {0xFFFFFFFFu, 1, 2};

int main(void) {
    // (1) ローカル unsigned 配列要素の直接比較
    unsigned a[3] = {0xFFFFFFFFu, 10, 20};
    if (!(a[0] == 0xFFFFFFFFu)) return 1;
    if (a[0] <= 0x7FFFFFFFu) return 2;

    // (2) グローバル unsigned 配列要素
    if (!(garr[0] == 0xFFFFFFFFu)) return 3;

    // (3) unsigned ポインタ deref
    unsigned *p = a;
    if (!(*p == 0xFFFFFFFFu)) return 4;
    if (*p <= 0x7FFFFFFFu) return 5;

    // (4) ポインタ算術後の deref
    unsigned u = 0xFFFFFFFEu;
    unsigned *q = &u;
    if (!(*q > 0x80000000u)) return 6;

    // signed は従来どおり (回帰ガード)
    int sa[2] = {-1, 0};
    if (!(sa[0] < 0)) return 7;

    return 0;
}
