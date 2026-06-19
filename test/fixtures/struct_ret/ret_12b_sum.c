// 12 byte struct を戻り値で返す (ARM64 では x0/x1 ペア)
// 期待: exit=42 (10+20+12)
#include <assert.h>
struct Triple { int a; int b; int c; };
struct Triple make_triple(int x, int y, int z) {
    struct Triple t = {x, y, z};
    return t;
}
int main(void) {
    struct Triple r = make_triple(10, 20, 12);
    assert(r.a == 10);
    assert(r.b == 20);
    assert(r.c == 12);
    return 0;
}
