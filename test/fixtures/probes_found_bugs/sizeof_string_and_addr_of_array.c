// 2 つの sizeof バグ:
//  S1: sizeof(文字列リテラル) がエスケープシーケンスをソースの生バイト数で数えて
//      いた (sizeof("\t")==3)。デコード後のバイト数 (==2) を使うよう修正。併せて
//      グローバル `char g[]="a\tb"` がエスケープを未デコードで格納していたのも修正。
//  A1: sizeof(&array) が要素サイズ (4) を返していた。&array は `int(*)[N]` で 8。
#include <assert.h>

char g_str[] = "a\tb";   /* グローバル: デコードされ a,<tab>,b,\0 の 4 バイト */

int main(void) {
    /* S1: 文字列リテラルの sizeof はデコード後長 */
    assert(sizeof("\t") == 2);
    assert(sizeof("\xff") == 2);
    assert(sizeof("a\0b") == 4);
    assert(sizeof("a\tb" "c") == 5);   /* 連結 */
    assert(sizeof("abc") == 4);         /* エスケープなしは不変 */

    /* グローバル char 配列のエスケープがデコードされている */
    assert(sizeof(g_str) == 4);
    assert(g_str[1] == '\t');

    /* A1: &array はポインタ (8 バイト) */
    int a[4];
    assert(sizeof(&a) == 8);
    assert(sizeof(a) == 16);            /* 配列全体は不変 */
    assert(sizeof(&a[0]) == 8);

    /* &array を値として使う経路も維持 */
    int b[4] = { 1, 2, 30, 4 };
    int (*p)[4] = &b;
    assert((*p)[2] == 30);
    return 0;
}
