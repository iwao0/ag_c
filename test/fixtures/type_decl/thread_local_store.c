// _Thread_local への代入
// 期待: exit=0
#include <assert.h>
_Thread_local int tl_s = 0;
int main(void) {
    tl_s = 99;
    assert(tl_s == 99);
    return 0;
}
