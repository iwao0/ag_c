// _Thread_local read-modify-write の連鎖
// 期待: exit=0
#include <assert.h>
_Thread_local int tw = 1;
int main(void) {
    tw = tw + 1;
    tw = tw + 1;
    tw = tw + 1;
    tw = tw + 1;
    assert(tw == 5);
    return 0;
}
