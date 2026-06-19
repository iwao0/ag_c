// nibble swap: 0xAB → 0xBA = 186
// 期待: exit=186
#include <assert.h>
int main(void) {
    int a = 0xAB;
    int hi = (a >> 4) & 0x0F;
    int lo = a & 0x0F;
    assert(((lo << 4) | hi) == 186);
    return 0;
}
