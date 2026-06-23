// 続き55: 整数定数式の int オーバーフロー検出 (W3023, clang -Winteger-overflow 相当)。
//
// add() / mul() で両辺が int リテラル (int_is_long=0, int_is_long_long=0, !is_unsigned,
// fp_kind==NONE, 値が int 範囲内) のとき、ADD/SUB/MUL 結果が int32 範囲を超えるなら警告。
//
// 抑制条件 (合法形):
//   - long リテラル混在 (`2147483647L + 1L` など) — int 演算ではない
//   - 浮動小数点演算 (`1.5 * 1e10`) — overflow ではなく丸め
//   - unsigned 演算 — モジュロ演算で定義されている
//   - 変数を含む式 (`x + 1`) — 値が静的に確定しない
//
// 本 fixture は合法形のみ。
#include <assert.h>

int main(void) {
    /* (a) long リテラル混在は OK (int 演算ではない) */
    long la = 2147483647L + 1L;
    long lb = 2147483647 + 1L;       /* int + long → long */
    assert(la == 2147483648L);
    assert(lb == 2147483648L);

    /* (b) 浮動小数点演算は対象外 */
    double da = 1.5 * 1e10;
    assert(da > 1e10);

    /* (c) unsigned 演算は対象外 */
    unsigned ua = 0xffffffffU + 1U;  /* unsigned ラップアラウンドは定義済み */
    assert(ua == 0);

    /* (d) 変数を含む式は対象外 */
    int x = 100;
    int y = x + 1000;
    assert(y == 1100);

    /* (e) int 範囲内の演算 */
    int a = 1000000 + 2000000;
    int b = 50000 * 40000;            /* 2 * 10^9 → int 範囲内 */
    assert(a == 3000000);
    assert(b == 2000000000);

    return 0;
}
