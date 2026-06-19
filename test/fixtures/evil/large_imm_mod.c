// 16 bit 超の即値の剰余
// 100000 % 256 = 160
// 期待: exit=160
#include <assert.h>
int main(void) {
    assert(100000 % 256 == 160);
    return 0;
}
