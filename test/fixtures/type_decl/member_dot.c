// struct メンバの . アクセス
// 期待: exit=7
int main(void) {
    struct S { int a; int b; };
    struct S s;
    s.a = 2;
    s.b = 5;
    return s.a + s.b;
}
