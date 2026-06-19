// struct ポインタの -> アクセス
// 期待: exit=7
#include <assert.h>
int main(void) {
    struct S { int a; int b; };
    struct S s;
    struct S *p = &s;
    p->a = 3;
    p->b = 4;
    assert(p->a + p->b == 7);
    return 0;
}
