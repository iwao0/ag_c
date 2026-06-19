// _Thread_local 演算
// 期待: exit=0
#include <assert.h>
_Thread_local int tl_a = 10;
int main(void) {
    tl_a = tl_a + 5;
    assert(tl_a == 15);
    return 0;
}
