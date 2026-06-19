// 2D VLA: 外側 n も内側 m も実行時に決まるケース
// 10+5+20+7 = 42
// 期待: exit=42
#include <assert.h>
int main(void) {
    int n = 2;
    int m = 3;
    int a[n][m];
    a[0][0] = 10;
    a[0][2] = 5;
    a[1][1] = 20;
    a[1][2] = 7;
    assert(a[0][0] == 10);
    assert(a[0][2] == 5);
    assert(a[1][1] == 20);
    assert(a[1][2] == 7);
    return 0;
}
