// 空のマクロ実引数 (`F(7,)` / `F(,8)` / `F(a,,c)`) が E1024 で拒否されていた。
// C99 6.10.3p4 では空引数は合法で、placemarker として何も展開しない。引数の個数の
// 検査は残しつつ、空引数自体ではエラーにしないよう修正。
#include <assert.h>

#define F(a, b) (a b)
#define G(a, b, c) (a b c)
#define PICK_C(a, b, c) (c)

int main(void) {
    assert(F(7, ) == 7);     /* 末尾が空 → (7 ) */
    assert(F(, 8) == 8);     /* 先頭が空 → ( 8) */
    assert(G(1, , ) == 1);   /* 後ろ 2 つが空 */
    assert(G(, 2, ) == 2);   /* 中間のみ */
    assert(G(, , 3) == 3);   /* 末尾のみ */
    assert(PICK_C(10, 20, 5) == 5);
    assert(PICK_C(, , 9) == 9);  /* 前 2 引数が空 */
    return 0;
}
