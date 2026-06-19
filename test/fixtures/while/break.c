// while 内の break
// 期待: exit=1
#include <assert.h>
int main(void) {
    int a = 0;
    while (1) {
        a = a + 1;
        break;
    }
    assert(a == 1);
    return 0;
}
