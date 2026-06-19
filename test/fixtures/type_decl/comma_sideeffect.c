// カンマ式での副作用順序: f() を 3 回呼んで a=3, g=3 → 3*100+3 = 303 → mod 256 = 47
// 期待: exit=47
#include <assert.h>
int g = 0;
int f(void) { g = g + 1; return g; }
int main(void) {
    int a = (f(), f(), f());
    assert(a * 100 + g == 303);
    return 0;
}
