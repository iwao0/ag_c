// 続き69: `int g = (int)3.7;` のような浮動小数→整数キャストのグローバル初期化 (修正)。
//
// 修正前: apply_cast の TK_INT 分岐が浮動小数 ND_NUM をまず wrap_fp_to_int_if_needed で
// ND_FP_TO_INT にラップしてから整数化を試みていたが、その時点で operand->kind が
// ND_NUM ではなくなっており、続く「ND_NUM なら値を 32bit 切り詰めて返す」分岐に乗らない。
// 結果 init_expr は ND_FP_TO_INT のまま、apply_toplevel_object_initializer が受理できる
// 形 (NUM/ADDR/ADD/STRING/FUNCREF) に該当せず、has_init が立たず `.comm` (0 初期化)。
//
// 修正: ND_NUM (fp_kind != NONE) は wrap する前に直接 fval を truncate して
// 整数 ND_NUM を返す。これで定数 fold が維持され、グローバル init で受理される。
#include <assert.h>

/* 浮動小数リテラルから整数へのキャスト */
int g_int_pos = (int)3.7;             /* 3 (truncates) */
int g_int_neg = -(int)4.5;            /* (int)4.5 = 4、その後単項マイナスで -4 */
int g_int_zero = (int)0.0;            /* 0 */

/* float / long も同じ経路を通る (回帰確認) */
long g_long = (long)2.5;              /* 2 */
char g_char = (char)100.9;            /* 100 */

int main(void) {
    assert(g_int_pos == 3);
    assert(g_int_neg == -4);
    assert(g_int_zero == 0);
    assert(g_long == 2);
    assert(g_char == 100);

    /* ローカル変数でも同様に動作することを確認 */
    int local = (int)3.7;
    assert(local == 3);

    return 0;
}
