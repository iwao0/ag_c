// ビットフィールド b 側の読み出し
// 期待: exit=10
#include <assert.h>
int main(void) {
    struct S { unsigned int a:3; unsigned int b:5; };
    struct S s;
    s.a = 5;
    s.b = 10;
    assert(s.b == 10);
    return 0;
}
