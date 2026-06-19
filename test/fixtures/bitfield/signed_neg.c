// 符号付きビットフィールドへの -1 書き込みは負と判定される
// 期待: exit=42
#include <assert.h>
int main(void) {
    struct S { int f:4; };
    struct S s;
    s.f = -1;
    assert(s.f < 0);
    return 0;
}
