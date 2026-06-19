// unsigned int の大きな値の除算
// 0xFFFFFFFF / 2 == 0x7FFFFFFF
// 期待: exit=1
#include <assert.h>
int main(void) {
    unsigned int x = 4294967295u;
    assert(x / 2 == 2147483647u);
    return 0;
}
