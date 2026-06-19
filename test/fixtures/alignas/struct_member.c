// 構造体メンバへの _Alignas(8) で sizeof が 16 になること
// (char 1 byte + padding 7 + _Alignas(8) int 4 + padding 4 = 16)
// 期待: exit=42 (sizeof が 16 でなければ 0)
#include <assert.h>
int main(void) {
    struct S { char pad; _Alignas(8) int x; };
    assert((int)sizeof(struct S) == 16);
    return 0;
}
