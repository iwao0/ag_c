// unsigned int 除算 100/7 = 14
// 期待: exit=14
#include <assert.h>
int main(void) {
    unsigned int a = 100;
    unsigned int b = 7;
    assert(a / b == 14);
    return 0;
}
