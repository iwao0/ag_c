// struct の指定初期化子 (順不同)
// 期待: exit=3 (1+2)
int main(void) {
    struct S { int x; int y; };
    struct S s = {.y = 2, .x = 1};
    return s.x + s.y;
}
