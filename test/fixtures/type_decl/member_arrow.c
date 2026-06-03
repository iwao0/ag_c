// struct ポインタの -> アクセス
// 期待: exit=7
int main(void) {
    struct S { int a; int b; };
    struct S s;
    struct S *p = &s;
    p->a = 3;
    p->b = 4;
    return p->a + p->b;
}
