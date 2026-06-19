// typedef した const int* がマッチ
// 期待: exit=0
#include <assert.h>
typedef const int *cip_t;
int main(void) {
    int x = 0;
    cip_t p = &x;
    assert(_Generic(p, int*: 1, const int*: 2, default: 3) == 2);
    return 0;
}
