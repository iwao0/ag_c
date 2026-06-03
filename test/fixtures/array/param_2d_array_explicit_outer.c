// 2D 配列パラメータで外側サイズを明示する形 `int a[2][3]`
// C11 6.7.6.3p7 により最外側 [2] は pointer に「調整」される (サイズ無視)。
// したがって `int a[][3]` と同じ挙動になる。
// 期待: exit=7 (a[0][0]=1, a[1][2]=6)
int get(int a[2][3], int i, int j) {
    return a[i][j];
}
int main(void) {
    int a[2][3] = {{1,2,3}, {4,5,6}};
    return get(a, 0, 0) + get(a, 1, 2);
}
