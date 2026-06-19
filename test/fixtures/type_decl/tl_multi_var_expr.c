// 複数 _Thread_local 変数
// 期待: exit=0
#include <assert.h>
_Thread_local int ta = 2;
_Thread_local int tb = 3;
int main(void) {
    assert(ta * 10 + tb * 5 == 35);
    return 0;
}
