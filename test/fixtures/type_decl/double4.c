// double 除算
// 期待: exit=0
#include <assert.h>
int main(void) {
    double a = 15.0; double b = 3.0; double r = a / b;
    double diff = r - 5.0;
    if (diff < 0.0) diff = -diff;
    assert(diff < 1e-9);
    return 0;
}
