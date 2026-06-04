// struct 配列のネスト brace 初期化
// struct P a[3] = {{1, 2}, {3, 4}, {5, 6}}; → a[2].x=5, a[2].y=6
// 期待: exit=11 (5+6)
struct P { int x, y; };
int main(void) {
    struct P a[3] = {{1, 2}, {3, 4}, {5, 6}};
    return a[2].x + a[2].y;
}
