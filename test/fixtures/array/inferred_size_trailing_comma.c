// 配列サイズ推定: 末尾カンマあり (要素数に含めない)
// 期待: exit=15 (1+2+3+4+5)
#include <assert.h>
int main(void) {
    int a[] = {1, 2, 3, 4, 5,};
    assert(a[0] == 1);
    assert(a[1] == 2);
    assert(a[2] == 3);
    assert(a[3] == 4);
    assert(a[4] == 5);
    return 0;
}
