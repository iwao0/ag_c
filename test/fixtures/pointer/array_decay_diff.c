// 配列名は式中で pointer に decay する。&a[9] - a は要素差 (= 9) になる。
// 期待: exit=9
int main(void) {
    int a[10];
    return (int)(&a[9] - a);
}
