// struct 配列のネスト brace 初期化
// struct P a[3] = {{1, 2}, {3, 4}, {5, 6}}; → a[2].x=5, a[2].y=6
// 期待: exit=11 (5+6)
#include <assert.h>
struct P { int x, y; };
int main(void) {
    struct P a[3] = {{1, 2}, {3, 4}, {5, 6}};
    assert(a[0].x == 1);
    assert(a[0].y == 2);
    assert(a[1].x == 3);
    assert(a[1].y == 4);
    assert(a[2].x == 5);
    assert(a[2].y == 6);
    return 0;
}
