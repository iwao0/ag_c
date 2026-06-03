// 複合リテラルのメンバへの代入は lvalue
// 期待: exit=5
int main(void) {
    struct S { int x; };
    return (((struct S){1}).x = 5);
}
