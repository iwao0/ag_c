// ネスト指定で複数要素 .a[0]=7, .a[2]=5
// 期待: exit=12
int main(void) {
    struct S { int a[3]; };
    struct S s = {.a[0] = 7, .a[2] = 5};
    return s.a[0] + s.a[2];
}
