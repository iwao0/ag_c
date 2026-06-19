// 3 次元配列 `int a[][2][3]` を仮引数に取る関数
// 修正前: SEGV (lvar が単純 int* 扱いで多次元情報が消えていた)
// 対応: 内側の 2 つの `[N]` を捕捉し、outer_stride=N1*N2*elem,
//      mid_stride=N2*elem を設定。pointee は 2D 配列としてアクセスされる。
// 期待: exit=9 (a[1][0][2])
#include <assert.h>
int get(int a[][2][3], int i, int j, int k) {
    return a[i][j][k];
}
int main(void) {
    int a[2][2][3] = {{{1,2,3},{4,5,6}}, {{7,8,9},{10,11,12}}};
    assert(get(a, 0, 0, 0) == 1);
    assert(get(a, 0, 0, 1) == 2);
    assert(get(a, 0, 0, 2) == 3);
    assert(get(a, 0, 1, 0) == 4);
    assert(get(a, 0, 1, 1) == 5);
    assert(get(a, 0, 1, 2) == 6);
    assert(get(a, 1, 0, 0) == 7);
    assert(get(a, 1, 0, 1) == 8);
    assert(get(a, 1, 0, 2) == 9);
    assert(get(a, 1, 1, 0) == 10);
    assert(get(a, 1, 1, 1) == 11);
    assert(get(a, 1, 1, 2) == 12);
    return 0;
}
