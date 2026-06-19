// 幅 4 のビットフィールドに 15 (=0xF) を書く (収まる)
// 期待: exit=15
#include <assert.h>
int main(void) {
    struct S { unsigned int f:4; };
    struct S s;
    s.f = 15;
    assert(s.f == 15);
    return 0;
}
