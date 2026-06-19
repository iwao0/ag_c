// 2D VLA (内側定数 3) を 2 重ループで埋めて合計
// 0+1+2+3+4+5 = 15
// 期待: exit=15
#include <assert.h>
int main(void) {
    int n = 2;
    int sum = 0;
    int a[n][3];
    int i;
    for (i = 0; i < n; i++) {
        int j;
        for (j = 0; j < 3; j++) a[i][j] = i * 3 + j;
    }
    assert(a[0][0] == 0);
    assert(a[0][1] == 1);
    assert(a[0][2] == 2);
    assert(a[1][0] == 3);
    assert(a[1][1] == 4);
    assert(a[1][2] == 5);
    for (i = 0; i < n; i++) {
        int j;
        for (j = 0; j < 3; j++) sum += a[i][j];
    }
    assert(sum == 15);
    return 0;
}
