// `char a[] = {"..."}` — ブレースで包んだ文字列リテラル初期化
// 修正前: 配列サイズが「トップレベル要素数 1」と推定され、'h' しかコピー
//        されなかった (exit=104)。C11 6.7.9p14 により素の文字列初期化と
//        同じに扱うべき。
// 期待: exit=215 ('h'=104, 'o'=111)
#include <assert.h>
int main(void) {
    char a[] = { "hello" };
    assert(a[0] == 'h');
    assert(a[4] == 'o');
    assert(a[5] == 0);
    assert(sizeof(a) == 6);
    return 0;
}
