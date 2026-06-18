// 指定初期化子で歯抜けにすると、未指定要素は 0 で埋められる (C11 6.7.9p21)
// int a[5] = {[2]=100, [4]=1} → a[0]=0, a[1]=0, a[2]=100, a[3]=0, a[4]=1
// 期待: exit=101 (a[2]+a[4]+a[0])
#include <assert.h>
int main(void) {
    int a[5] = { [2] = 100, [4] = 1 };
    assert(a[0] == 0);
    assert(a[1] == 0);
    assert(a[2] == 100);
    assert(a[3] == 0);
    assert(a[4] == 1);
    return 0;
}
