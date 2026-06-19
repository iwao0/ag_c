// `cond ? int : char` 等、三項の一方が sub-int (char/short) の場合、結果型は int。
// build_node_ternary は結果用の 4 バイト slot に各分岐を store するが、char 分岐を
// strb (1 バイト) で書いていたため上位 3 バイトが未初期化のまま残り、merge の
// 4 バイト ldrsw が garbage を読んで誤値になっていた (2 つ以上あると特に顕在化)。
// 分岐値は load 時に符号/ゼロ拡張済みなので、結果型へ retag して full-width store する
// (SEXT を挿入しない = unsigned char 分岐を誤って符号拡張しない)。
#include <assert.h>

int main(void) {
    int cond = 0;
    char ca = 10, cb = 20;
    int x = 100, y = 200;

    // 連続した char 分岐の三項: r1 が r2 計算後も保持される
    int r1 = cond ? x : ca;   // 10
    int r2 = cond ? y : cb;   // 20
    assert(r1 == 10);
    assert(r2 == 20);

    // signed char 分岐は符号拡張
    signed char sc = -5;
    int rs = cond ? x : sc;
    assert(rs == -5);

    // unsigned char 分岐はゼロ拡張 (符号拡張で -56 にならない)
    unsigned char uc = 200;
    int ru = cond ? x : uc;
    assert(ru == 200);

    // short 分岐
    short sh = -1000;
    int rh = cond ? x : sh;
    assert(rh == -1000);

    // true 側が char の場合
    int rt = (1) ? ca : x;
    assert(rt == 10);
    return 0;
}
