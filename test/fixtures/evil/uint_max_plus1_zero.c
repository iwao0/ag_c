// unsigned int のラップ (UINT_MAX+1 == 0)
// 期待: exit=1
#include <assert.h>
int main(void) {
    unsigned int x = 4294967295u;
    x = x + 1;
    assert(x == 0);
    return 0;
}
