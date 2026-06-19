// struct 単純代入で独立したコピーが作られること
// s.a=10, s2.a=99 → 10*10+99 = 199
// 期待: exit=199
#include <assert.h>
int main(void) {
    struct S { int a; int b; };
    struct S s;
    s.a = 10; s.b = 20;
    struct S s2 = s;
    s2.a = 99;
    assert(s.a * 10 + s2.a == 199);
    return 0;
}
