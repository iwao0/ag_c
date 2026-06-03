// struct 複合リテラル + メンバアクセス
// 期待: exit=2 (.y)
int main(void) {
    struct S { int x; int y; };
    return ((struct S){.x = 1, .y = 2}).y;
}
