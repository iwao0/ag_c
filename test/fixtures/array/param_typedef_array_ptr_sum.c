// typedef 配列型 ptr 仮引数で 2D 配列の全要素を加算
// 期待: exit=45 (1+2+...+9)
typedef int row_t[3];
int sum_row(row_t *a, int n) {
    int s = 0;
    int i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < 3; j++)
            s += a[i][j];
    return s;
}
int main(void) {
    int b[3][3] = {{1,2,3}, {4,5,6}, {7,8,9}};
    return sum_row(b, 3);
}
