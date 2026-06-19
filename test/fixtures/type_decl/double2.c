// double 加算
// 期待: exit=0
#include <assert.h>
int main(void) {
    double a = 3.1; double b = 4.2; double r = a + b;
    double diff = r - 7.3;
    if (diff < 0.0) diff = -diff;
    assert(diff < 1e-9);
    return 0;
}
