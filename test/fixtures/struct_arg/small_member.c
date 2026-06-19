// 小サイズ struct を値渡し → 個別メンバの取得
// 期待: exit=42
#include <assert.h>
struct Point { int x; int y; };
int get_y(struct Point p) { return p.y; }
int main(void) {
    struct Point pt = {10, 42};
    assert(pt.x == 10);
    assert(pt.y == 42);
    assert(get_y(pt) == 42);
    return 0;
}
