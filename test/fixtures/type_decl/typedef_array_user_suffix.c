// typedef 配列にユーザー追加の [N] suffix を結合する。
// arr[1][2][3] = 1*100 + 2*10 + 3 = 123
// 期待: exit=0
#include <assert.h>
typedef int M3[3][4];
int main(void) {
    M3 arr[2];
    int i, j, k;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 3; j++)
            for (k = 0; k < 4; k++)
                arr[i][j][k] = i * 100 + j * 10 + k;
    assert(arr[1][2][3] == 123);
    return 0;
}
