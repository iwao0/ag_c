// 2D 配列の外側サイズ推定 (3 行)
// `int a[][3] = {{1,2,3},{4,5,6},{7,8,9}}` で outer = 3 を推定。
// 期待: exit=9 (a[2][2])
#include <assert.h>
int main(void) {
    int a[][3] = {{1,2,3}, {4,5,6}, {7,8,9}};
    assert(a[0][0] == 1);
    assert(a[0][1] == 2);
    assert(a[0][2] == 3);
    assert(a[1][0] == 4);
    assert(a[1][1] == 5);
    assert(a[1][2] == 6);
    assert(a[2][0] == 7);
    assert(a[2][1] == 8);
    assert(a[2][2] == 9);
    return 0;
}
