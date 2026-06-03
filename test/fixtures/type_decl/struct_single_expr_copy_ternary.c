// 三項演算子の値を struct コピー初期化に
// 0 偽 → b={3,4} 採用 → 3+4 = 7
// 期待: exit=7
int main(void) {
    struct S { int x; int y; };
    struct S a = {1, 2};
    struct S b = {3, 4};
    struct S s = (0 ? a : b);
    return s.x + s.y;
}
