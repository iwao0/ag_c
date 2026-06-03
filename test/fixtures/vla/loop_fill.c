// VLA を for ループで埋めて合計
// 期待: exit=10 (0+1+2+3+4)
int main(void) {
    int n = 5;
    int a[n];
    int i;
    for (i = 0; i < n; i++) a[i] = i;
    return a[0] + a[1] + a[2] + a[3] + a[4];
}
