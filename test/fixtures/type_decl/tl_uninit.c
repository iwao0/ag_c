// _Thread_local の未初期化はゼロ
// 期待: exit=0
#include <assert.h>
_Thread_local int tu;
int main(void) {
    assert(tu == 0);
    return 0;
}
