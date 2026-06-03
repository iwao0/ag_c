// 小サイズ struct (≤8B) を値渡し → メンバの合計
// ARM64 ABI ではレジスタ 1 本で渡される。
// 期待: exit=7 (3+4)
struct Point { int x; int y; };
int sum(struct Point p) { return p.x + p.y; }
int main(void) {
    struct Point pt = {3, 4};
    return sum(pt);
}
