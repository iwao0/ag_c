// 配列サイズ推定: 指定初期化子 [N]= による位置追跡
// 期待: exit=111 (a[0]=1, a[3]=10, a[5]=100)
#include <assert.h>
int main(void) {
    int a[] = { [3] = 10, [0] = 1, [5] = 100 };
    assert(a[0] == 1);
    assert(a[1] == 0);
    assert(a[2] == 0);
    assert(a[3] == 10);
    assert(a[4] == 0);
    assert(a[5] == 100);
    return 0;
}
