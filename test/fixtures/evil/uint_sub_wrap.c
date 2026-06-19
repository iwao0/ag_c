// unsigned int の減算ラップ (10-20 → 0xFFFFFFF6 = 4294967286)
// 期待: exit=1
#include <assert.h>
int main(void) {
    unsigned int x = 10;
    unsigned int y = 20;
    unsigned int z = x - y;
    assert(z == 4294967286u);
    return 0;
}
