// 負数の除算は 0 方向に truncate (-7/2 == -3、-7%2 == -1)
// 期待: exit=2
#include <assert.h>
int main(void) {
    int x = -7;
    int y = 2;
    assert(x / y == -3);
    assert(x % y == -1);
    return 0;
}
