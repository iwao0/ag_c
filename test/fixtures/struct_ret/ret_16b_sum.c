// 16 byte struct を戻り値で返す (ARM64 では x0/x1 ペア境界)
// 期待: exit=42 (1+2+3+36)
struct Quad { int a; int b; int c; int d; };
struct Quad make_quad(int a, int b, int c, int d) {
    struct Quad q = {a, b, c, d};
    return q;
}
int main(void) {
    struct Quad r = make_quad(1, 2, 3, 36);
    return r.a + r.b + r.c + r.d;
}
