// _Atomic(int) 型指定子
// 期待: exit=0
#include <assert.h>
int main(void) {
    _Atomic(int) z = 5;
    assert(z == 5);
    return 0;
}
