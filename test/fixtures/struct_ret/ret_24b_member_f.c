// 24 byte struct を返して 6 番目のメンバ f を取る
#include <assert.h>
struct S6 { int a; int b; int c; int d; int e; int f; };
struct S6 make6(int x) {
    struct S6 s = {x, x+1, x+2, x+3, x+4, x+5};
    return s;
}
int main(void) {
    struct S6 r = make6(1);
    assert(r.f == 6);   // x+5 = 1+5
    return 0;
}
