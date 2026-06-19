// unsigned int 剰余 100%7 = 2
// 期待: exit=2
#include <assert.h>
int main(void) {
    unsigned int a = 100;
    unsigned int b = 7;
    assert(a % b == 2);
    return 0;
}
