// _Thread_local のアドレス取得
// 期待: exit=0
#include <assert.h>
_Thread_local int tv = 5;
int main(void) {
    int *p = &tv;
    assert(*p == 5);
    return 0;
}
