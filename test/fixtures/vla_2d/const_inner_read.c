// 2D VLA: 外側 n は実行時、内側は定数 (=4)
// 1+2+10+100 = 113
// 期待: exit=113
#include <assert.h>
int main(void) {
    int n = 3;
    int a[n][4];
    a[0][0] = 1;
    a[0][1] = 2;
    a[1][0] = 10;
    a[2][3] = 100;
    assert(a[0][0] == 1);
    assert(a[0][1] == 2);
    assert(a[1][0] == 10);
    assert(a[2][3] == 100);
    return 0;
}
