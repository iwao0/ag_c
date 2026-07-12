// 縮小変換 (narrowing) と自己比較 (tautological compare) の警告。
//
// 修正:
// (a) 整数変数を浮動小数点リテラルで初期化 `int x = 1.5;` (clang -Wliteral-conversion 相当)。
//     ps_decl_parse_initializer_for_var のスカラ非ポインタ非タグ分岐で init_expr が ND_NUM
//     かつ fp_kind != NONE かつ fval に小数部があれば W3001 warning。
//     ・var->fp_kind != NONE (`double d = 1.5;`) は対象外。
//     ・整数値の暗黙変換 (`int x = 2.0;`) は値が等価なので警告しない (fval == (long long)fval)。
// (b) 自己比較 `x == x` / `x != x` (clang -Wtautological-compare 相当)。equality() で
//     両辺が同じ ND_LVAR offset または同じ ND_GVAR 名なら W3001 warning。
//
// 本 fixture は合法形 (整数→整数、float→float、異なる変数同士の比較) の回帰確認。
#include <assert.h>

int main(void) {
    /* (a) 整数→整数の初期化は警告なし */
    int a = 42;
    assert(a == 42);

    /* float→float、float→整数 (値が一致する場合) も警告なし */
    double d = 1.5;
    assert(d > 1.4 && d < 1.6);

    /* 整数値を持つ float リテラル (1.0、3.0) は警告なし — 値が等価 */
    int b = 3.0;
    assert(b == 3);

    /* 明示キャスト経由 — 警告対象外 */
    int c = (int)1.7;
    assert(c == 1);

    /* (b) 異なる変数の比較は警告なし */
    int x = 5;
    int y = 5;
    assert(x == y);
    assert(!(x != y));

    /* グローバル変数同士でも別なら警告なし */
    /* (fixture 内 main 内なのでローカル比較で十分) */

    return 0;
}
