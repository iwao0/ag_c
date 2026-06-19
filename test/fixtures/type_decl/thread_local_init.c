// _Thread_local 初期化
// 期待: exit=0
#include <assert.h>
_Thread_local int tl_val = 7;
int main(void) {
    assert(tl_val == 7);
    return 0;
}
