// 続き45-47 の警告 false-positive 確認用 fixture。
//
// 修正で追加された warning:
// (a) 代入を条件として使う `if (x = 10)` / `while (x = 0)` (clang -Wparentheses 相当)。
//     parse_stmt_if / parse_stmt_while で条件式 top が ND_ASSIGN なら W3001。
// (b) 整数リテラルが型サイズを超える初期化 `char c = 300;`、`short s = 70000;`
//     (clang -Wconstant-conversion 相当)。decl.c のスカラ初期化分岐で var->elem_size < 4
//     かつ ND_NUM の値が型範囲外なら W3001。`unsigned char uc = -1;` は意図的なイディオム
//     として除外。
// (c) ローカル変数のアドレスを return (`return &x;`) (clang -Wreturn-stack-address 相当)。
//     parse_stmt_return で node->lhs が ND_ADDR(ND_LVAR) で static でないなら W3001。
//
// 本 fixture は合法形の回帰確認。
#include <assert.h>

/* (a) 通常の比較は警告なし */
int classify(int n) {
    if (n > 0) return 1;
    if (n < 0) return -1;
    return 0;
}

/* (b) 範囲内の整数初期化 */
int test_ranges(void) {
    char c = 100;          /* signed char 範囲内 (-128..127) */
    short s = 1000;        /* short 範囲内 */
    unsigned char uc = -1; /* 全ビット 1 のイディオム (255) */
    return c + s + uc;
}

/* (c) static ローカルのアドレスを返すのは合法 (寿命がプログラム期間) */
int *get_static_addr(void) {
    static int x = 42;
    return &x;
}

/* (c) グローバル変数のアドレスを返すのも合法 */
static int g_val = 7;
int *get_global_addr(void) {
    return &g_val;
}

/* (c) 引数で渡されたポインタを返すのは合法 (caller の責任) */
int *passthrough(int *p) {
    return p;
}

int main(void) {
    assert(classify(5) == 1);
    assert(classify(-5) == -1);
    assert(classify(0) == 0);
    int r = test_ranges();
    assert(r == 100 + 1000 + 255);
    assert(*get_static_addr() == 42);
    assert(*get_global_addr() == 7);
    int x = 99;
    assert(*passthrough(&x) == 99);
    return 0;
}
