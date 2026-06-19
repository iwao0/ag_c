// ポインタ加算でストライド (int は 4 byte)
// p+2 で 3 番目の要素 30 を指す
// 期待: exit=30
#include <assert.h>
int main(void) {
    int a[4];
    a[0]=10; a[1]=20; a[2]=30; a[3]=40;
    int *p = a;
    p = p + 2;
    assert(*p == 30);
    return 0;
}
