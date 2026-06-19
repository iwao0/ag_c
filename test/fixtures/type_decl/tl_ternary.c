// _Thread_local + 三項演算子
// 期待: exit=0
#include <assert.h>
_Thread_local int tt = 3;
int main(void) {
    assert((tt > 2 ? tt * tt : 0) == 9);
    return 0;
}
