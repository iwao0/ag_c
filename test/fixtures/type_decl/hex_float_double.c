// 16 進浮動小数点リテラル 0x1.8p+3 = 12.0
// 期待: exit=0
#include <assert.h>
int main(void) {
    double d = 0x1.8p+3;
    double diff = d - 12.0;
    if (diff < 0.0) diff = -diff;
    assert(diff < 1e-9);
    return 0;
}
