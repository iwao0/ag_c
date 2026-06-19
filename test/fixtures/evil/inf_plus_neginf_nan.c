// +Inf + -Inf = NaN
// 期待: exit=1
#include <assert.h>
int main(void) {
    double a = 1.0 / 0.0;
    double b = -1.0 / 0.0;
    double c = a + b;
    assert(c != c);
    return 0;
}
