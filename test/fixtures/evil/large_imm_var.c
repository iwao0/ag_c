// 大きな即値を変数経由で剰余
// 1000000 % 256 = 64
// 期待: exit=64
#include <assert.h>
int main(void) {
    int x = 1000000;
    assert(x % 256 == 64);
    return 0;
}
