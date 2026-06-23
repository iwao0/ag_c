// 続き66: グローバルポインタを「文字列リテラル + offset」で初期化する形 (修正)。
//
// 修正前: `const char *p = "abc" + 2;` のような形が resolve_global_addr_init で
// ND_STRING のケースを持たないため失敗し、has_init が立たず `_p` が `.comm`
// (uninitialized) として出力。実行時に NULL ポインタ deref で SIGSEGV。
//
// 修正:
// 1. parser.c::resolve_global_addr_init に ND_STRING 分岐を追加し、文字列ラベルを
//    ベースシンボル (sym_len=-1 sentinel) として返す。+offset は既存の ND_ADD 経路
//    で init_symbol_offset に蓄積される。
// 2. arm64_apple.c の出力経路で sentinel + offset 形を `.quad LABEL + N` で emit する
//    (修正前は sentinel 経路が offset を無視して `.quad LABEL` のみ出していた)。
#include <assert.h>

/* 文字列リテラル + 整数 offset で初期化 */
const char *str_at_2 = "abcdef" + 2;       /* "cdef" を指す */
const char *str_at_4 = "abcdef" + 4;       /* "ef" を指す */
const char *str_at_0 = "abcdef" + 0;       /* "abcdef" 先頭 (回帰確認: offset=0 でも動く) */
const char *str_plain = "hello";           /* 既存形 (offset なし) */

int main(void) {
    assert(str_at_2[0] == 'c');
    assert(str_at_2[1] == 'd');
    assert(str_at_2[3] == 'f');
    assert(str_at_2[4] == 0);

    assert(str_at_4[0] == 'e');
    assert(str_at_4[1] == 'f');
    assert(str_at_4[2] == 0);

    assert(str_at_0[0] == 'a');
    assert(str_at_0[5] == 'f');

    assert(str_plain[0] == 'h');
    assert(str_plain[4] == 'o');

    return 0;
}
