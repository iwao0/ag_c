// _Alignas / _Atomic 前置修飾
// 期待: exit=0
#include <assert.h>
int main(void) {
    _Alignas(16) int x = 3;
    _Atomic int y = 4;
    assert(x + y == 7);
    return 0;
}
