// `char a[] = {"abc" "de"}` — ブレース内 + 隣接文字列連結
// 連結結果は "abcde" (5 文字 + NUL → サイズ 6)
// 期待: exit=199 ('a'=97, 'e'=101, a[5]==0 → +1 = 199)
#include <assert.h>
int main(void) {
    char a[] = { "abc" "de" };
    assert(a[0] == 'a');
    assert(a[1] == 'b');
    assert(a[2] == 'c');
    assert(a[3] == 'd');
    assert(a[4] == 'e');
    assert(a[5] == 0);
    assert(sizeof(a) == 6);
    return 0;
}
