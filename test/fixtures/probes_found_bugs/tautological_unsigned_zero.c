// 続き51: 符号なし整数と 0 の常に同じ結果になる比較 (W3019, clang
// -Wtautological-unsigned-zero-compare 相当)。
//
//   `u >= 0` / `0 <= u`  -> 常に真
//   `u < 0`  / `0 > u`   -> 常に偽
//
// warn_if_tautological_unsigned_zero を relational() の <, <=, >, >= で呼ぶ。
// `u == 0`, `u != 0`, `u > 0`, `u <= 0` は値次第なので警告しない。
//
// 本 fixture は合法形 (警告が出てはいけない形) を網羅。
#include <assert.h>

int main(void) {
    unsigned int u = 5;
    int s = -1;

    /* (a) 値次第で結果が変わる形 — 警告なし */
    if (u == 0) return 1;
    if (u != 0) {}     // (この経路は警告対象外)
    if (u > 0) {}
    if (u <= 0) return 2;
    if (0 == u) return 3;
    if (0 < u) {}
    if (0 >= u) return 4;

    /* (b) signed 整数と 0 の比較は値次第なので警告なし */
    if (s >= 0) return 5;
    if (s < 0) {}      // s は negative なのでこれは真

    /* (c) 浮動小数点 vs 0 は対象外 */
    double d = 0.5;
    if (d >= 0.0) {}

    assert(1);
    return 0;
}
