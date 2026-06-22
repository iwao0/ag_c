// コンパイル時に検出可能な未定義動作 / 怪しい書き方の警告。
//
// 修正:
// (a) 0 リテラルでの除算・剰余 `1 / 0` / `1 % 0` (C11 6.5.5p5 未定義動作)。mul() の
//     ND_DIV / ND_MOD 構築時に rhs が ND_NUM(0) なら W3001 warning。
// (b) シフト量が型の幅を超える `1 << 32` / `1 << -1` (C11 6.5.7p3 未定義動作)。shift() の
//     ND_SHL / ND_SHR 構築時に rhs が ND_NUM で型幅以上 (int=32, long=64) または負なら警告。
// (c) 自己代入 `x = x` (タイプミスの可能性、clang -Wself-assign 相当)。assign() の
//     TK_ASSIGN 分岐で両辺が同じ ND_LVAR offset なら警告。
// (d) 空 if 本体 `if (cond);` (clang -Wempty-body 相当)。parse_stmt_if で `)` の直後が
//     `;` (TK_SEMI) なら警告。
//
// 本 fixture は合法形 (非ゼロ除算、適切なシフト、別変数への代入、本体ありの if) の
// 回帰確認。
#include <assert.h>

int main(void) {
    /* (a) 非ゼロ除算は OK */
    int q = 10 / 3;        /* 3 */
    int r = 10 % 3;        /* 1 */
    assert(q == 3 && r == 1);

    /* (b) 適切なシフト量 */
    int s1 = 1 << 0;       /* 1 */
    int s2 = 1 << 31;      /* 0x80000000 (= INT_MIN だが implementation-defined ではなく合法な値) */
    long ls = 1L << 63;    /* 64-bit shift OK */
    (void)s2; (void)ls;
    assert(s1 == 1);

    /* (c) 別変数への代入 */
    int a = 5;
    int b = a;             /* not self */
    a = b + 1;             /* not self (rhs is expression, not just a) */
    assert(a == 6);

    /* (d) 本体ありの if (合法) */
    int x = 0;
    if (a > 0) x = 1;
    if (a > 0) { x = 2; }
    assert(x == 2);

    return 0;
}
