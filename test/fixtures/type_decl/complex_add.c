// _Complex 加算
// 期待: exit=1
#include <assert.h>
int main(void) {
    _Complex double a = 3.0;
    _Complex double b = 2.0;
    _Complex double c = a + b;
    _Complex double d = 5.0;
    assert(c == d);
    return 0;
}
