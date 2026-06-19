// 大サイズ struct (>16B) を値渡し
// ARM64 ABI ではアドレス渡し (byref) になる。
// 期待: exit=15 (1+2+3+4+5)
#include <assert.h>
struct Big { int a; int b; int c; int d; int e; };
int sum5(struct Big p) { return p.a + p.b + p.c + p.d + p.e; }
int main(void) {
    struct Big b = {1, 2, 3, 4, 5};
    assert(b.a == 1);
    assert(b.b == 2);
    assert(b.c == 3);
    assert(b.d == 4);
    assert(b.e == 5);
    assert(sum5(b) == 15);
    return 0;
}
