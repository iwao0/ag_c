// typedef した volatile int* がマッチ
// 期待: exit=0
#include <assert.h>
typedef volatile int *vip_t;
int main(void) {
    int x = 0;
    vip_t p = &x;
    assert(_Generic(p, volatile int*: 2, int*: 1, default: 3) == 2);
    return 0;
}
