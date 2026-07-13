// 定数 0 を条件にすると else 側
// 期待: exit=3
#include <assert.h>
int main() {
    int r;
    if (0) r = 2;
    else r = 3;
    assert(r == 3);
    return 0;
}
