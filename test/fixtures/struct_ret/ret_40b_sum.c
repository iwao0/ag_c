// 40 byte struct を返す (大きめ indirect return)
#include <assert.h>
struct Big10 { int a; int b; int c; int d; int e; int f; int g; int h; int i; int j; };
struct Big10 make10() {
    struct Big10 s = {1,2,3,4,5,6,7,8,9,10};
    return s;
}
int main(void) {
    struct Big10 r = make10();
    assert(r.a == 1);
    assert(r.b == 2);
    assert(r.c == 3);
    assert(r.d == 4);
    assert(r.e == 5);
    assert(r.f == 6);
    assert(r.g == 7);
    assert(r.h == 8);
    assert(r.i == 9);
    assert(r.j == 10);
    return 0;
}
