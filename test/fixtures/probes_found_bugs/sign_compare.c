// 続き48: signed/unsigned 比較警告 (W3018 / clang -Wsign-compare 相当)。
//
// C11 6.3.1.8 通常算術変換のうち signed→unsigned 変換が起きる比較を検出する。
// 抑制条件:
//   - 整数昇格で signed int になる狭い型 (size<4) は signed 扱い (unsigned char 含む)
//   - 非負整数リテラルは値が変わらないので安全
//   - signed 側が unsigned 側より厳密に大きい型 (例: long vs unsigned int) は安全
//   - 浮動小数・ポインタ比較は対象外
//
// 本 fixture は合法形 (警告が出てはいけない形) を網羅する。
// 真の signed/unsigned 不一致は既存の cmp_same_width_unsigned.c が回帰確認している。
#include <assert.h>

int main(void) {
    /* (1) 非負リテラル比較は安全 */
    unsigned int u = 5;
    if (u == 0) {}
    if (u >= 3) {}
    if (u < 10) {}

    /* (2) narrow unsigned (char/short) は integer promotion で signed int になる */
    unsigned char uc = 200;
    unsigned short us = 50000;
    int s = 1000;
    if (uc < s) {}     // unsigned char → int (signed) 比較
    if (us > s) {}     // unsigned short → int (signed) 比較

    /* (3) 広い signed (long) vs 狭い unsigned (unsigned int) は signed 比較に変換される */
    long sl = -1;
    unsigned int u32 = 1;
    if (sl < u32) {}    // long が unsigned int を含むため signed 比較で正しく動く

    /* (4) 同じ符号同士は警告なし */
    int s1 = 1, s2 = 2;
    unsigned u1 = 1, u2 = 2;
    if (s1 < s2) {}
    if (u1 < u2) {}

    /* (5) 浮動小数比較は対象外 */
    double d = 1.5;
    if (d > 1.0) {}

    assert(1);
    return 0;
}
