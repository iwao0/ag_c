// 8 バイトを超える struct の compound literal を関数へ値渡しすると、先頭 8 バイト
// だけが渡され後半メンバが落ちていたバグ。
// 原因: struct 引数のポインタ渡し経路が arg->kind == ND_LVAR のみ対象で、compound
//   literal (`(struct V){...}` = ND_COMMA(init, ND_LVAR temp)) が漏れて値ロード
//   (先頭 8B) されていた。9-16B struct の x1 分 (後半メンバ) が garbage に。
// 修正: arg が ND_COMMA(init, struct ND_LVAR) のとき init を評価してから temp lvar を
//   struct 引数 (ポインタ渡し) として扱う。
// 修正前: 後半メンバが化ける (3 メンバ struct で v.c が garbage)
// 期待: exit=42
#include <assert.h>
struct V3 { int a, b, c; };       // 12 バイト (9-16B レンジ)
struct V5 { int a, b, c, d, e; }; // 20 バイト (>16B レンジ)

int sum3(struct V3 v) { return v.a + v.b + v.c; }
int sum5(struct V5 v) { return v.a + v.b + v.c + v.d + v.e; }

int main(void) {
    // 単一 compound literal (12B)
    if (sum3((struct V3){1, 2, 3}) != 6) return 1;

    // 複数 compound literal (それぞれ別 temp)
    int two = sum3((struct V3){1, 2, 3}) + sum3((struct V3){10, 20, 30});
    assert(two == 66);

    // >16B compound literal
    if (sum5((struct V5){1, 2, 3, 4, 5}) != 15) return 3;

    // compound literal と struct 変数の混在
    struct V3 t = {100, 200, 300};
    if (sum3((struct V3){7, 8, 9}) + sum3(t) != 624) return 4;

    assert(sum3((struct V3){12, 14, 16}) == 42); return 0;  // 12+14+16 = 42
}
