// nibble マスク OR: 0xFF → (lo nibble) | (hi nibble) = 0xF | 0xF = 0xF = 15
// 期待: exit=15
#include <assert.h>
int main(void) {
    int a = 0xFF;
    assert(((a & 0x0F) | ((a >> 4) & 0x0F)) == 15);
    return 0;
}
