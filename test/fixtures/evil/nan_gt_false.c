// NaN > 0.0 は偽
// 期待: exit=1
#include <assert.h>
int main(void) {
    double x = 0.0 / 0.0;
    _Bool gt = (x > 0.0);   // NaN との順序比較は偽
    assert(gt == 0);
    return 0;
}
