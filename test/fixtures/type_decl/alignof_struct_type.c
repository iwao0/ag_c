// _Alignof(struct S) = 4
// 期待: exit=4
int main(void) {
    struct S { int x; };
    return _Alignof(struct S);
}
