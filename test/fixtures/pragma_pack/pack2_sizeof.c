// pack(2) で char a + 1 byte padding + int b で 6 byte
// 期待: exit=6
#pragma pack(push, 2)
struct S { char a; int b; };
#pragma pack(pop)
#include <assert.h>
int main(void) {
    assert((int)sizeof(struct S) == 6);
    return 0;
}
