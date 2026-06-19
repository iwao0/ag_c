// struct 配列のネスト brace で、要素内のメンバが省略されたら 0 埋め
// struct P a[2] = {{10}, {20, 30}}; → a[0].y=0
// 合計 = 10 + 0 + 20 + 30 = 60
// 期待: exit=60
#include <assert.h>
struct P { int x, y; };
int main(void) {
    struct P a[2] = {{10}, {20, 30}};
    assert(a[0].x == 10);
    assert(a[0].y == 0);
    assert(a[1].x == 20);
    assert(a[1].y == 30);
    return 0;
}
