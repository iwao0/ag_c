// struct 配列のネスト brace で、要素内のメンバが省略されたら 0 埋め
// struct P a[2] = {{10}, {20, 30}}; → a[0].y=0
// 合計 = 10 + 0 + 20 + 30 = 60
// 期待: exit=60
struct P { int x, y; };
int main(void) {
    struct P a[2] = {{10}, {20, 30}};
    return a[0].x + a[0].y + a[1].x + a[1].y;
}
