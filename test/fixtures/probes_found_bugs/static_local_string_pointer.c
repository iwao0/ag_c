// 続き61: `static const char *msg = "hello";` のような static local 文字列ポインタ
// 初期化と subscript アクセス (修正)。
//
// 修正前: 2 つの独立した不具合が組み合わさっていた。
// (1) try_lower_static_local_scalar が非定数 init (文字列リテラル等) を silently 0 に
//     落としており、`static char *msg = "hello"` のポインタ値が NULL になっていた。
// (2) build_lvar_or_vla_node の static-local 早期 return が ND_GVAR の is_pointer を
//     設定せず、`msg[i]` の subscript 経路が「両辺非ポインタ」と判定して E3064 を
//     出していた。
//
// 修正:
// (1) try_lower_static_local_scalar の頭で peek し、サポート外の init 形 (`= "..."`,
//     `= {`, 識別子参照、関数呼び出し等) なら 0 を返して auto local 経路へ fall through。
// (2) static-local の ND_GVAR 生成で size>elem_size のとき is_pointer=1 を立てる。
#include <assert.h>

const char *get_msg(void) {
    /* (1) 文字列ポインタ static local */
    static const char *msg = "hello";
    return msg;
}

int read_first(void) {
    /* (2) static local + subscript */
    static const char *msg = "hello";
    return msg[0];
}

int sum_chars(void) {
    /* (3) 複数回 subscript */
    static const char *s = "abc";
    return s[0] + s[1] + s[2];  /* a + b + c */
}

int main(void) {
    /* (1) ポインタ値が "hello" を指している */
    const char *m = get_msg();
    assert(m[0] == 'h');
    assert(m[1] == 'e');
    assert(m[4] == 'o');
    assert(m[5] == 0);

    /* (2) subscript アクセス */
    assert(read_first() == 'h');

    /* (3) 連続 subscript */
    assert(sum_chars() == 'a' + 'b' + 'c');

    /* (4) 整数の static local (回帰確認 — 既存経路を壊していないこと) */
    static int x = 42;
    assert(x == 42);
    x = 99;
    assert(x == 99);

    /* (5) 整数の static local (符号付きリテラル — peek が `+`/`-` を受理する) */
    static int y = -5;
    static int z = +7;
    assert(y == -5);
    assert(z == 7);

    return 0;
}
