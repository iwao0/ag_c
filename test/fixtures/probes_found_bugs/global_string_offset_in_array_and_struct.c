// 続き67: 配列・struct メンバとして「文字列リテラル + offset」を初期化する形 (修正)。
//
// 修正前: 続き66 で単一 `const char *p = "..." + N;` は通したが、配列要素や struct
// メンバとして同形を書くと codegen の sentinel 経路 (sym_len<0) が +offset を無視して
// `.quad LABEL` のみ出していた。要素ポインタが先頭を指してしまい、subscript が
// 期待と違う文字を返していた。
//
// 修正: arm64_apple.c の 2 箇所 (emit_global_init_member_scalar と inline ループ) で
// sentinel + offset 形を `.quad LABEL + N` で emit。
#include <assert.h>

/* 配列要素として「文字列 + offset」 */
const char *items[3] = { "alpha" + 1, "beta" + 0, "gamma" + 2 };

/* struct メンバとして同形 */
struct Pair { const char *a; const char *b; };
struct Pair g_p = { "hello" + 1, "world" + 2 };

/* 配列 of struct: 各要素の各メンバが offset 形 */
struct Item { const char *name; int code; };
struct Item entries[] = {
    { "first" + 1, 100 },
    { "second" + 2, 200 },
    { "third" + 0, 300 },
};

int main(void) {
    /* 配列 of string + offset */
    assert(items[0][0] == 'l');   /* "alpha"+1 -> "lpha" */
    assert(items[1][0] == 'b');
    assert(items[2][0] == 'm');   /* "gamma"+2 -> "mma" */

    /* struct メンバ */
    assert(g_p.a[0] == 'e');      /* "hello"+1 -> "ello" */
    assert(g_p.b[0] == 'r');      /* "world"+2 -> "rld" */

    /* 配列 of struct */
    assert(entries[0].name[0] == 'i');  /* "first"+1 -> "irst" */
    assert(entries[0].code == 100);
    assert(entries[1].name[0] == 'c');  /* "second"+2 -> "cond" */
    assert(entries[1].code == 200);
    assert(entries[2].name[0] == 't');  /* "third"+0 -> "third" */
    assert(entries[2].code == 300);

    return 0;
}
