// 構造体の部分初期化: 残りメンバは 0 で埋められる (C11 6.7.9p21)
// struct S s = {10, 20} → s.c, s.d = 0、合計 = 30
// 期待: exit=30
int main(void) {
    struct S { int a, b, c, d; };
    struct S s = {10, 20};
    return s.a + s.b + s.c + s.d;
}
