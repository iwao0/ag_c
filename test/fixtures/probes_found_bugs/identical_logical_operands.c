// 続き52: `x && x` / `x || x` の同一オペランド検出 (W3020, gcc -Wlogical-op 相当)。
//
// logical_and() / logical_or() で両辺が ND_LVAR (同 offset)、ND_GVAR (同 name)、
// ND_NUM (同 値+fp) のいずれかなら W3020。副作用のある式 (関数呼び出し等) は
// AST 比較で一致しないので警告されない。
//
// 本 fixture は合法形 (警告が出てはいけない形) のみ。
#include <assert.h>

int g_count = 0;
int side_effect(int v) { g_count++; return v; }

int main(void) {
    int x = 1, y = 2;

    /* (a) 異なる変数 — 警告なし */
    if (x && y) {}
    if (x || y) {}

    /* (b) 異なる比較式 — 警告なし */
    if (x > 0 && y > 0) {}
    if (x == 1 || y == 2) {}

    /* (c) 同じリテラル同士は同一扱い (`1 && 1` 等) になるが、これは現実には書かない
     *     基本形ではないので扱いを問わない。実装上は AST 等価判定にひっかかれば警告。
     *     ここでは異なる即値で false positive がないことのみ確認。 */
    if (1 || 0) {}    // ND_NUM(1) と ND_NUM(0) は別
    if (1 && 0) {}    // 同上

    /* (d) 副作用のある関数呼び出しは AST が ND_CALL/ND_FUNCALL なので比較対象外、
     *     `f() && f()` のような明示的な「2 回呼ぶ」イディオムを誤検出しない。 */
    if (side_effect(1) && side_effect(2)) {}
    if (side_effect(1) || side_effect(2)) {}
    assert(g_count == 3);  // 1 つ目: 真 → 2 つ目評価、2 つ目以降: 1 つ目真→短絡

    return 0;
}
