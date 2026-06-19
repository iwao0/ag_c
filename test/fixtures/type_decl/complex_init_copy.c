// _Complex double の初期化と == 比較
// 期待: exit=1
#include <assert.h>
int main(void) {
    _Complex double a = 3.0;
    _Complex double b = a;
    _Complex double c = 3.0;
    assert(b == c);
    return 0;
}
