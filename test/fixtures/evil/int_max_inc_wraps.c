// INT_MAX++ → 負数 (実装定義だが ag_c は wrap)
// 期待: exit=1
#include <assert.h>
int main(void) {
    int x = 2147483647;
    x++;
    assert(x == -2147483648);
    assert(x < 0);
    return 0;
}
