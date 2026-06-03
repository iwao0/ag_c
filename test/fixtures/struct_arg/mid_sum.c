// 中サイズ struct (9-16B) を値渡し
// ARM64 ABI ではレジスタ 2 本で渡される。
// 期待: exit=42 (10+20+12)
struct Mid { int a; int b; int c; };
int sum3(struct Mid p) { return p.a + p.b + p.c; }
int main(void) {
    struct Mid m = {10, 20, 12};
    return sum3(m);
}
