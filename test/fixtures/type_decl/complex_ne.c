// _Complex の不等価比較
// 期待: exit=0
#include <assert.h>
int main(void) {
    _Complex double a = 3.0;
    _Complex double b = 4.0;
    assert(a != b);
    return 0;
}
