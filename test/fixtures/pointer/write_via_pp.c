// 2 段ポインタ経由で元変数に書き込み
// 期待: exit=77
int main(void) {
    int x = 10;
    int *p = &x;
    int **pp = &p;
    **pp = 77;
    return x;
}
