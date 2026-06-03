// (*p).x と p->x の等価性
// 期待: exit=10 (5+5)
int main(void) {
    struct S { int x; };
    struct S a = {5};
    struct S *p = &a;
    return (*p).x + p->x;
}
