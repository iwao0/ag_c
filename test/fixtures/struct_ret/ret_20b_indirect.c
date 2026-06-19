// 20 byte struct を戻り値で返す (ARM64 ABI: indirect return via x8)
#include <assert.h>
struct Big { int a; int b; int c; int d; int e; };
struct Big make_big(int v) {
    struct Big b = {v, v+1, v+2, v+3, v+4};
    return b;
}
int main(void) {
    struct Big r = make_big(5);
    assert(r.a == 5);
    assert(r.b == 6);
    assert(r.c == 7);
    assert(r.d == 8);
    assert(r.e == 9);
    return 0;
}
