// 2D 配列の外側サイズ推定 (フラット初期化子)
// フラット要素数を内側次元で切り上げ、canonical outer length を確定する。
#include <assert.h>
int main(void) {
    int a[][3] = { 1, 2, 3, 4, 5, 6 };
    assert(a[0][0] == 1);
    assert(a[0][1] == 2);
    assert(a[0][2] == 3);
    assert(a[1][0] == 4);
    assert(a[1][1] == 5);
    assert(a[1][2] == 6);

    int partial[][3] = { 1, 2, 3, 4, 5 };
    assert(sizeof(partial) == 24);
    assert(partial[1][0] == 4);
    assert(partial[1][1] == 5);
    assert(partial[1][2] == 0);
    return 0;
}
