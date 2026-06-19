// ファイルスコープの複合リテラル初期化子 `T g = (T){...};` が `,` で E2006 と
// なりコンパイル失敗していた (ローカルは正常)。`T g = (T){...}` は `T g = {...}` と
// 等価 (C11 6.5.2.5) なので、先頭の `(型)` を読み飛ばして既存の brace 初期化に渡す。
#include <assert.h>

struct S { int a, b; };
struct S g_struct = (struct S){ 3, 4 };
int g_scalar = (int){ 5 };
int g_arr[3] = (int[3]){ 1, 2, 3 };

/* 通常の初期化子が壊れていないことも確認 */
struct S g_plain = { 10, 20 };
int g_expr = (1 + 2) * 3;        /* 括弧式 (複合リテラルではない) */

int main(void) {
    assert(g_struct.a == 3 && g_struct.b == 4);
    assert(g_scalar == 5);
    assert(g_arr[0] == 1 && g_arr[1] == 2 && g_arr[2] == 3);
    assert(g_plain.a == 10 && g_plain.b == 20);
    assert(g_expr == 9);
    return 0;
}
