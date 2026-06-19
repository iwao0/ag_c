// 同サイズ別タグ struct 間 cast (ag_c 拡張)
// 期待: exit=7
#include <assert.h>
int main(void) {
    struct A { int x; };
    struct B { int x; };
    struct A a = {7};
    struct B b = (struct B)a;
    assert(b.x == 7);
    return 0;
}
