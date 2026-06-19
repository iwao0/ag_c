// unsigned int の右シフトは sign extension しない
// 0x80000000 >> 1 == 0x40000000
// 期待: exit=1
#include <assert.h>
int main(void) {
    unsigned int x = 0x80000000u;
    unsigned int y = x >> 1;
    assert(y == 0x40000000u);
    return 0;
}
