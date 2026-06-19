// double 初期化と return
// 期待: exit=0
#include <assert.h>
int main(void) {
    double d = 3.99;
    double diff = d - 3.99;
    if (diff < 0.0) diff = -diff;
    assert(diff < 1e-9);
    return 0;
}
