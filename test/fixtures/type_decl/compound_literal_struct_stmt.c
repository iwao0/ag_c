// struct 複合リテラルを文として書く (副作用なし、return 7)
// 期待: exit=7
int main(void) {
    struct S { int x; int y; };
    (struct S){1, 2};
    return 7;
}
