// 小サイズ struct を値渡し → 個別メンバの取得
// 期待: exit=42
struct Point { int x; int y; };
int get_y(struct Point p) { return p.y; }
int main(void) {
    struct Point pt = {10, 42};
    return get_y(pt);
}
