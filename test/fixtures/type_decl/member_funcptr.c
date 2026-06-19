// struct メンバの関数ポインタを呼ぶ
// 期待: exit=42
#include <assert.h>
int inc(int x) { return x + 1; }
int main(void) {
    struct S { int (*fp)(int); };
    struct S s;
    s.fp = inc;
    assert(s.fp(41) == 42);
    return 0;
}
