// 続き68: グローバル初期化での「同一シンボル上のポインタ減算」(修正)。
//
// 修正前: `long g_diff = &g_arr[3] - &g_arr[1];` のような ICE は、内部表現が
// ND_DIV(ND_SUB(addr3, addr1), sizeof_elem) になり、resolve_global_addr_init が
// ND_DIV を処理しないため has_init が立たず `.comm` (零初期化) に落ちていた。
// 実行時に g_diff は 0 として読まれ、`return (int)g_diff - 2` が -2 = 254。
//
// 修正: decl.c::eval_const_expr_decl の ND_SUB 共通分岐の末尾で、両辺が
// 同一シンボルの (sym, off) に resolve できる場合は (loff - roff) を返すように拡張。
// 上位の ND_DIV(..., 4) はそのまま既存の二項演算経路で除算する。
// parser.c の resolve_global_addr_init を psx_resolve_global_addr_init として
// 公開し、decl.c から呼べるようにした。
#include <assert.h>

int g_arr[5] = {10, 20, 30, 40, 50};

/* 要素単位の距離 (= ND_DIV(ND_SUB(...), sizeof(int)) ) */
long g_diff = &g_arr[3] - &g_arr[1];   /* 2 */
long g_full = &g_arr[4] - &g_arr[0];   /* 4 */
long g_zero = &g_arr[2] - &g_arr[2];   /* 0 */
long g_neg  = &g_arr[1] - &g_arr[4];   /* -3 */

/* バイト距離 (char*) */
long g_bytes = (char*)&g_arr[2] - (char*)&g_arr[0];  /* 8 */

int main(void) {
    assert(g_diff == 2);
    assert(g_full == 4);
    assert(g_zero == 0);
    assert(g_neg == -3);
    assert(g_bytes == 8);
    return 0;
}
