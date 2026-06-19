// 3 次元 typedef 配列の `&a` を pointer-to-array 仮引数として関数に渡す。
// a[1][2][3] = 1*100 + 2*10 + 3 = 123
// 期待: exit=0
#include <assert.h>
typedef int M[2][3][4];
int f(M *p) {
    return (*p)[1][2][3];
}
int main(void) {
    M a;
    int i, j, k;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 3; j++)
            for (k = 0; k < 4; k++)
                a[i][j][k] = i * 100 + j * 10 + k;
    assert(f(&a) == 123);
    return 0;
}
