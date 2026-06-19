// (*p).x と p->x の等価性
// 期待: exit=10 (5+5)
#include <assert.h>
int main(void) {
    struct S { int x; };
    struct S a = {5};
    struct S *p = &a;
    assert((*p).x == 5);
    assert(p->x == 5);
    assert((*p).x + p->x == 10);
    return 0;
}
