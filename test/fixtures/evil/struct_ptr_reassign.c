// struct ポインタの再代入で別 struct を読む
// r1=10, r2=20 → 30
// 期待: exit=30
int main(void) {
    struct S { int x; };
    struct S a = {10};
    struct S b = {20};
    struct S *p;
    p = &a;
    int r1 = p->x;
    p = &b;
    int r2 = p->x;
    return r1 + r2;
}
