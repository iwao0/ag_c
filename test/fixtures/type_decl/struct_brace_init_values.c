// struct 波括弧初期化値の確認
// 期待: exit=3 (1+2)
int main(void) {
    struct S { int x; int y; };
    struct S s = {1, 2};
    return s.x + s.y;
}
