// 配列へのポインタ `int (*p)[3]` で 2D アクセス
// 期待: exit=6 (a[1][2])
int main(void) {
    int a[2][3] = {{1,2,3}, {4,5,6}};
    int (*p)[3] = a;
    return p[1][2];
}
