// 小サイズ struct (≤8B) を値渡し → メンバの合計
// ARM64 ABI ではレジスタ 1 本で渡される。
// 期待: exit=7 (3+4)
#include <assert.h>
struct Point { int x; int y; };
int sum(struct Point p) { return p.x + p.y; }
int main(void) {
    struct Point pt = {3, 4};
    assert(pt.x == 3);
    assert(pt.y == 4);
    assert(sum(pt) == 7);
    return 0;
}
