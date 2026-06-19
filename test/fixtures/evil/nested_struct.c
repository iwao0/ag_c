// ネストした struct のメンバアクセス
// 1 + 2*10 + 3*100 = 321、 mod 256 = 65
// 期待: exit=65
#include <assert.h>
int main(void) {
    struct A { int x; struct B { int y; int z; } b; };
    struct A a;
    a.x = 1;
    a.b.y = 2;
    a.b.z = 3;
    assert(a.x == 1);
    assert(a.b.y == 2);
    assert(a.b.z == 3);
    return 0;
}
