// _Complex の乗算チェーン (2*2*2 = 8)
// 期待: exit=0
#include <assert.h>
int main(void) {
    _Complex double a = 2.0;
    _Complex double b = a * a;
    _Complex double c = b * a;
    _Complex double e = 8.0;
    assert(c == e);
    return 0;
}
