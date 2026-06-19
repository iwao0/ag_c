// struct を関数戻り値で返す
// p.x^2 + p.y^2 = 9+16 = 25
// 期待: exit=25
#include <assert.h>
struct P { int x; int y; };
struct P make(int x, int y) {
    struct P p;
    p.x = x;
    p.y = y;
    return p;
}
int main(void) {
    struct P p = make(3, 4);
    assert(p.x * p.x + p.y * p.y == 25);
    return 0;
}
