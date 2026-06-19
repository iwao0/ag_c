// typedef による不完全配列型
// 期待: exit=1
#include <assert.h>
typedef int A[];
int main(void) {
    A *p = 0;
    assert((p == 0) == 1);
    return 0;
}
