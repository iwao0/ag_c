// _Complex 乗算
// 期待: exit=0
#include <assert.h>
int main(void) {
    _Complex double a = 3.0;
    _Complex double b = 4.0;
    _Complex double c = a * b;
    _Complex double d = 12.0;
    assert(c == d);
    return 0;
}
