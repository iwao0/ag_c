// _Thread_local を switch 条件に
// 期待: exit=0
#include <assert.h>
_Thread_local int tsw = 2;
int get_val(void) {
    switch (tsw) {
        case 1: return 10;
        case 2: return 20;
        default: return 99;
    }
}
int main(void) {
    assert(get_val() == 20);
    return 0;
}
