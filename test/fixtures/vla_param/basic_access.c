// 仮引数 VLA 宣言子 `int a[n]` → `int *a` として扱われる (C11 6.7.6.3p7)
// 期待: exit=15 (1+2+3+4+5)
int sum_arr(int n, int a[n]) {
    int s = 0;
    int i;
    for (i = 0; i < n; i++) s += a[i];
    return s;
}
int main(void) {
    int n = 5;
    int a[n];
    int i;
    for (i = 0; i < n; i++) a[i] = i + 1;
    return sum_arr(n, a);
}
