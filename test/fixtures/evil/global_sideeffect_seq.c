// グローバル副作用の評価順序 (左から)
// a=1, b=2, c=3 → 100+20+3 = 123
// 期待: exit=123
#include <assert.h>
int g_step = 0;
int next(void) { g_step = g_step + 1; return g_step; }
int main(void) {
    int a = next();
    int b = next();
    int c = next();
    assert(a == 1);
    assert(b == 2);
    assert(c == 3);
    assert(a * 100 + b * 10 + c == 123);
    return 0;
}
