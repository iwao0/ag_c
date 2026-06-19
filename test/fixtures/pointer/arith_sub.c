// ポインタ減算
// p+3 で 4 番目、 p-1 で 3 番目 = 30
// 期待: exit=30
#include <assert.h>
int main(void) {
    int a[4];
    a[0]=10; a[1]=20; a[2]=30; a[3]=40;
    int *p = a;
    p = p + 3;
    p = p - 1;
    assert(*p == 30);
    return 0;
}
