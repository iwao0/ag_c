// 仮引数 VLA に対する sizeof はポインタサイズ (=8)
// 期待: exit=8
int get_size(int n, int a[n]) {
    return (int)sizeof(a);
}
int main(void) {
    int n = 10;
    int a[n];
    return get_size(n, a);
}
