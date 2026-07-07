// _Thread_local aggregate initialization
// Expected: exit=0
#include <assert.h>

struct pair {
    int x;
    int y;
};

_Thread_local int tl_arr[4] = {3, 5};
_Thread_local struct pair tl_pair = {7, 11};

int main(void) {
    assert(tl_arr[0] == 3);
    assert(tl_arr[1] == 5);
    assert(tl_arr[2] == 0);
    assert(tl_arr[3] == 0);
    assert(tl_pair.x == 7);
    assert(tl_pair.y == 11);
    return 0;
}
