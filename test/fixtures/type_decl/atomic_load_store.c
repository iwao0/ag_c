// _Atomic int の load/store
// 期待: exit=0
#include <assert.h>
int main(void) {
    _Atomic int x = 10;
    int y = x + 32;
    assert(y == 42);
    return 0;
}
