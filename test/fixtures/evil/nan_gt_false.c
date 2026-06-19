// NaN > 0.0 は偽
// 期待: exit=1
#include <assert.h>
int main(void) {
    double x = 0.0 / 0.0;
    assert(!(x > 0.0));
    return 0;
}
