// struct メンバの . アクセス
// 期待: exit=7
#include <assert.h>
int main(void) {
    struct S { int a; int b; };
    struct S s;
    s.a = 2;
    s.b = 5;
    assert(s.a + s.b == 7);
    return 0;
}
