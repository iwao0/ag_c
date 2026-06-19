// pack(1) でメンバ b のオフセットが詰まっても値の読み書きが正しいこと
// 期待: exit=42 (1 + 41)
#pragma pack(push, 1)
struct S { char a; int b; };
#pragma pack(pop)
#include <assert.h>
int main(void) {
    struct S s;
    s.a = 1;
    s.b = 41;
    assert(s.a == 1);
    assert(s.b == 41);
    return 0;
}
