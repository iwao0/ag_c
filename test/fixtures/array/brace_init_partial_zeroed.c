// 部分初期化: 残り要素は 0 で埋められる (C11 6.7.9p21)
// int a[5] = {1, 2} → a[2..4] = 0、合計 = 1+2+0+0+0 = 3
// 期待: exit=3
#include <assert.h>
int main(void) {
    int a[5] = {1, 2};
    assert(a[0] == 1);
    assert(a[1] == 2);
    assert(a[2] == 0);
    assert(a[3] == 0);
    assert(a[4] == 0);
    return 0;
}
