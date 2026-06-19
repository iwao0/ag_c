// 16 進浮動小数点 0x1p4 = 16.0
// 期待: exit=0
#include <assert.h>
int main(void) {
    double d = 0x1p4;
    double diff = d - 16.0;
    if (diff < 0.0) diff = -diff;
    assert(diff < 1e-9);
    return 0;
}
