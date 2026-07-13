// 定数 1 を条件にすると if 側
// 期待: exit=2
#include <assert.h>
int main() {
    int r;
    if (1) r = 2;
    else r = 3;
    assert(r == 2);
    return 0;
}
