// NaN == NaN は偽
// 期待: exit=1
#include <assert.h>
int main(void) {
    double x = 0.0 / 0.0;
    assert(x != x);   // NaN は自身と等しくない
    return 0;
}
