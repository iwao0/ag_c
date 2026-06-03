// 配列仮引数の static/restrict 修飾子 (パース確認)
// 期待: exit=7
int f(int a[static 3], int b[restrict static 2]) { return 7; }
int main(void) {
    return f(0, 0);
}
