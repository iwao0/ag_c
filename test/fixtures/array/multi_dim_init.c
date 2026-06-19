// 2D 配列のネスト初期化と添字
// 期待: exit=6 (a[1][2])
#include <assert.h>
int main(void) {
    int a[2][3] = {{1,2,3}, {4,5,6}};
    assert(a[0][0] == 1);
    assert(a[0][1] == 2);
    assert(a[0][2] == 3);
    assert(a[1][0] == 4);
    assert(a[1][1] == 5);
    assert(a[1][2] == 6);
    return 0;
}
