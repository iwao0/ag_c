// 3 次元 typedef 配列をローカル変数として使う。
// `typedef int M3[2][3][4]; M3 a;` で a は int[2][3][4] と等価。
// a[1][2][3] = 1*100 + 2*10 + 3 = 123
// 期待: exit=0
#include <assert.h>
typedef int M3[2][3][4];
int main(void) {
    M3 a;
    int i, j, k;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 3; j++)
            for (k = 0; k < 4; k++)
                a[i][j][k] = i * 100 + j * 10 + k;
    assert(a[1][2][3] == 123);
    return 0;
}
