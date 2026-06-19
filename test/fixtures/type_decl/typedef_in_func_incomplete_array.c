// 関数内 typedef で不完全配列型
// 期待: exit=1
#include <assert.h>
int main(void) {
    typedef int A[];
    A *p = 0;
    assert(p == 0);
    return 0;
}
