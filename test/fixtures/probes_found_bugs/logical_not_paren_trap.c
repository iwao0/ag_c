// 続き53: `!x == y` の優先順位罠を W3021 で警告 (clang -Wlogical-not-parentheses 相当)。
//
// `!` は `==` / `!=` より優先順位が高いため `!p == 0` は `(!p) == 0` と解釈される。
// ag_c は `!x` を ND_EQ(x, 0) に変換し、その node に from_logical_not フラグを立てる。
// equality() で `==` / `!=` の LHS が from_logical_not なら警告。
//
// 本 fixture は合法形のみ。
#include <assert.h>

int main(void) {
    int *p = 0;
    int x = 5;

    /* (a) `!` 単独 — 警告なし */
    if (!p) {}
    if (!x) {}
    int b = !x;
    (void)b;

    /* (b) `==` 単独 — 警告なし */
    if (x == 0) {}
    if (p == (void*)0) {}

    /* (c) `!` を括弧で囲んだ `!(x == y)` は ND_EQ が直接 `!` から来ていない
     *     ので警告なし (`!(...)` は EQ ノードで from_logical_not が立つが、その
     *     ND_EQ が等価式の LHS にならない -> 警告判定の対象外)。 */
    if (!(x == 5)) {}
    if (!(x != 5)) {}

    /* (d) 通常の連鎖 `(a == b) == c` も警告なし (from_logical_not が立たない) */
    int a = 1, b2 = 1, c = 1;
    if ((a == b2) == c) {}

    assert(1);
    return 0;
}
