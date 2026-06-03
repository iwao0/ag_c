// 2D VLA (両次元実行時) を 2 重ループで埋めて合計
// 0+1+...+11 = 66
// 期待: exit=66
int main(void) {
    int n = 3;
    int m = 4;
    int sum = 0;
    int a[n][m];
    int i;
    for (i = 0; i < n; i++) {
        int j;
        for (j = 0; j < m; j++) a[i][j] = i * m + j;
    }
    for (i = 0; i < n; i++) {
        int j;
        for (j = 0; j < m; j++) sum += a[i][j];
    }
    return sum;
}
