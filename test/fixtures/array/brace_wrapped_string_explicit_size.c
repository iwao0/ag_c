// `char a[10] = {"hi"}` — 明示サイズ + ブレース内文字列
// "hi" の 2 文字を a[0..1] に、a[2] 以降は 0 で埋める。
// 期待: exit=210 ('h'=104, 'i'=105, a[2]==0 → +1 = 210)
#include <assert.h>
int main(void) {
    char a[10] = { "hi" };
    assert(a[0] == 'h');
    assert(a[1] == 'i');
    assert(a[2] == 0);
    assert(a[9] == 0);
    assert(sizeof(a) == 10);
    return 0;
}
