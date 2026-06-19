// unsigned int の乗算ラップ (65536*65536 == 0)
// 期待: exit=1
#include <assert.h>
int main(void) {
    unsigned int x = 65536u;
    unsigned int y = x * x;
    assert(y == 0);
    return 0;
}
