// _Complex 減算
// 期待: exit=0
#include <assert.h>
int main(void) {
    _Complex double a = 5.0;
    _Complex double b = 3.0;
    _Complex double c = a - b;
    _Complex double d = 2.0;
    assert(c == d);
    return 0;
}
